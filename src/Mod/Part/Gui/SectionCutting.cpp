/***************************************************************************
 *   Copyright (c) 2022 Uwe Stöhr <uwestoehr@lyx.org>                      *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/nodes/SoCamera.h>
# include <Inventor/nodes/SoClipPlane.h>
# include <Inventor/nodes/SoGroup.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/sensors/SoTimerSensor.h>
# include <QDialog>
# include <QDockWidget>
# include <QPointer>
//# include <cmath>
#endif

#include "SectionCutting.h"
#include "ui_SectionCutting.h"
#include <App/Link.h>
#include <App/Part.h>
#include <Base/UnitsApi.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/DockWindowManager.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Mod/Part/App/FeatureCompound.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/FeaturePartCommon.h>
#include <Mod/Part/App/FeaturePartCut.h>
#include <Mod/Part/App/FeaturePartFuse.h>
#include <Mod/Part/App/PartFeatures.h>

using namespace PartGui;

enum Refresh : bool
{
    notXValue = false,
    notYValue = false,
    notZValue = false,
    notXRange = false,
    notYRange = false,
    notZRange = false,
    XValue = true,
    YValue = true,
    ZValue = true,
    XRange = true,
    YRange = true,
    ZRange = true
};

SectionCut::SectionCut(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui_SectionCut)
{
    // create widgets
    ui->setupUi(this);
    ui->cutX->setRange(-INT_MAX, INT_MAX);
    ui->cutY->setRange(-INT_MAX, INT_MAX);
    ui->cutZ->setRange(-INT_MAX, INT_MAX);

    // get all objects in the document
    doc = Gui::Application::Instance->activeDocument()->getDocument();
    if (!doc) {
        Base::Console().Error("SectionCut error: there is no document\n");
        return;
    }

    std::vector<App::DocumentObject*> ObjectsList = doc->getObjects();
    if (ObjectsList.empty()) {
        Base::Console().Error("SectionCut error: there are no objects in the document\n");
        return;
    }
    // empty the ObjectsListVisible
    ObjectsListVisible.clear();
    // now store those that are currently visible
    for (auto it = ObjectsList.begin(); it != ObjectsList.end(); ++it) {
        if ((*it)->Visibility.getValue())
            ObjectsListVisible.push_back((*it));
    }

    // we can have existing cut boxes take their values
    // the flip state cannot be readout of the box position, therefore readout the position
    // is if it was unflipped
    if (doc->getObject(BoxZName)) {
        hasBoxZ = true;
        ui->groupBoxZ->setChecked(true);
        Part::Box* pcBox = static_cast<Part::Box*>(doc->getObject(BoxZName));
        if (!pcBox) {
            Base::Console().Error("SectionCut error: cut box is incorrectly named, cannot proceed\n");
            return;
        }
        ui->cutZ->setValue(pcBox->Height.getValue() - fabs(pcBox->Placement.getValue().getPosition().z));
    }
    if (doc->getObject(BoxYName)) {
        hasBoxY = true;
        ui->groupBoxY->setChecked(true);
        Part::Box* pcBox = static_cast<Part::Box*>(doc->getObject(BoxYName));
        if (!pcBox) {
            Base::Console().Error("SectionCut error: cut box is incorrectly named, cannot proceed\n");
            return;
        }
        ui->cutY->setValue(pcBox->Width.getValue() - fabs(pcBox->Placement.getValue().getPosition().y));
    }
    if (doc->getObject(BoxXName)) {
        hasBoxX = true;
        ui->groupBoxX->setChecked(true);
        Part::Box* pcBox = static_cast<Part::Box*>(doc->getObject(BoxXName));
        if (!pcBox) {
            Base::Console().Error("SectionCut error: cut box is incorrectly named, cannot proceed\n");
            return;
        }
        ui->cutX->setValue(pcBox->Length.getValue() - fabs(pcBox->Placement.getValue().getPosition().x));
    }

    // hide existing cuts to check if there are objects to be cut visible
    if (doc->getObject(CutXName))
        doc->getObject(CutXName)->Visibility.setValue(false);
    if (doc->getObject(CutYName))
        doc->getObject(CutYName)->Visibility.setValue(false);
    if (doc->getObject(CutZName))
        doc->getObject(CutZName)->Visibility.setValue(false);

    // get bounding box
    SbBox3f box = getViewBoundingBox();
    if (!box.isEmpty()) {
        // if there is a cut box, perform the cut
        if (hasBoxX || hasBoxY || hasBoxZ) {
            // refresh only the range since we set the values above already
            refreshCutRanges(box, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
                Refresh::XRange, Refresh::YRange, Refresh::ZRange);
        }
        else
            refreshCutRanges(box);
    }
    // the case of an empty box and having cuts will be handles later by startCutting(true)

    connect(ui->groupBoxX, &QGroupBox::toggled, this, &SectionCut::onGroupBoxXtoggled);
    connect(ui->groupBoxY, &QGroupBox::toggled, this, &SectionCut::onGroupBoxYtoggled);
    connect(ui->groupBoxZ, &QGroupBox::toggled, this, &SectionCut::onGroupBoxZtoggled);
    connect(ui->cutX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SectionCut::onCutXvalueChanged);
    connect(ui->cutY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SectionCut::onCutYvalueChanged);
    connect(ui->cutZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SectionCut::onCutZvalueChanged);
    connect(ui->cutXHS, &QSlider::sliderMoved, this, &SectionCut::onCutXHSsliderMoved);
    connect(ui->cutYHS, &QSlider::sliderMoved, this, &SectionCut::onCutYHSsliderMoved);
    connect(ui->cutZHS, &QSlider::sliderMoved, this, &SectionCut::onCutZHSsliderMoved);
    connect(ui->flipX, &QPushButton::clicked, this, &SectionCut::onFlipXclicked);
    connect(ui->flipY, &QPushButton::clicked, this, &SectionCut::onFlipYclicked);
    connect(ui->flipZ, &QPushButton::clicked, this, &SectionCut::onFlipZclicked);
    connect(ui->RefreshCutPB, &QPushButton::clicked, this, &SectionCut::onRefreshCutPBclicked);
    
    // if there is a cut, perform it
    if (hasBoxX || hasBoxY || hasBoxZ) {
        ui->RefreshCutPB->setEnabled(false);
        startCutting(true);
    }
}

// actions to be done when document was closed
void SectionCut::noDocumentActions()
{
    blockSignals(true);
    doc = nullptr;
    // reset the cut group boxes
    ui->groupBoxX->setChecked(false);
    ui->groupBoxY->setChecked(false);
    ui->groupBoxZ->setChecked(false);
    ui->RefreshCutPB->setEnabled(true);
    blockSignals(false);
}

void SectionCut::startCutting(bool isInitial)
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    // the document might have been changed
    if (doc != Gui::Application::Instance->activeDocument()->getDocument())
        // refresh documents list
        onRefreshCutPBclicked();

    // we will reuse it several times
    std::vector<App::DocumentObject*>::iterator it;

    // delete the objects we might have already created to cut
    // we must do this because we support several cuts at once and
    // it is dangerous to deal with the fact that the user is free
    // to uncheck cutting planes and to add/remove objects while this dialog is open
    // We must remove in this order because the tree hierary of the features is Z->Y->X and Cut->Box
    App::DocumentObject* anObject;
    if (doc->getObject(CutZName)) {
        anObject = doc->getObject(CutZName);
        // the deleted object might have been visible before, thus check and delete it from the list
        for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it) == anObject) {
                ObjectsListVisible.erase((it));
                break;
            }
        }
        doc->removeObject(CutZName);
    }
    if (doc->getObject(BoxZName)) {
        anObject = doc->getObject(BoxZName);
        for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it) == anObject) {
                ObjectsListVisible.erase((it));
                break;
            }
        }
        doc->removeObject(BoxZName);
    }
    if (doc->getObject(CutYName)) {
        anObject = doc->getObject(CutYName);
        for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it) == anObject) {
                ObjectsListVisible.erase((it));
                break;
            }
        }
        doc->removeObject(CutYName);
    }
    if (doc->getObject(BoxYName)) {
        anObject = doc->getObject(BoxYName);
        for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it) == anObject) {
                ObjectsListVisible.erase((it));
                break;
            }
        }
        doc->removeObject(BoxYName);
    }
    if (doc->getObject(CutXName)) {
        anObject = doc->getObject(CutXName);
        for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it) == anObject) {
                ObjectsListVisible.erase((it));
                break;
            }
        }
        doc->removeObject(CutXName);
    }
    if (doc->getObject(BoxXName)) {
        anObject = doc->getObject(BoxXName);
        for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it) == anObject) {
                ObjectsListVisible.erase((it));
                break;
            }
        }
        doc->removeObject(BoxXName);
    }
    if (doc->getObject(CompoundName)) {
        auto compoundObject = doc->getObject(CompoundName);
        Part::Compound* pcCompoundDel = static_cast<Part::Compound*>(compoundObject);
        std::vector<App::DocumentObject*> compoundObjects;
        pcCompoundDel->Links.getLinks(compoundObjects);
        // first delete the compound
        for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it) == anObject) {
                ObjectsListVisible.erase((it));
                break;
            }
        }
        doc->removeObject(CompoundName);
        // now delete the objects that have been part of the compound
        for (it = compoundObjects.begin(); it != compoundObjects.end(); it++) {
            for (auto itOV = ObjectsListVisible.begin(); itOV != ObjectsListVisible.end(); ++itOV) {
                if ((*itOV) == doc->getObject((*it)->getNameInDocument())) {
                    ObjectsListVisible.erase((itOV));
                    break;
                }
            }
            doc->removeObject((*it)->getNameInDocument());
        }
    }

    // make all objects visible that have been visible when the dialog was called
    // because we made them invisible when we created cuts
    for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
        if ((*it)->isValid()) // a formerly visible object might have been deleted
            (*it)->Visibility.setValue(true);
        else {
            // we must refresh the ObjectsListVisible list
            onRefreshCutPBclicked();
        }
    }

    // we enable the sliders because for assemblies we disabled them
    ui->cutXHS->setEnabled(true);
    ui->cutYHS->setEnabled(true);
    ui->cutZHS->setEnabled(true);

    // ObjectsListVisible contains all visible objects of the document, but we can only cut
    // those that have a solid shape
    std::vector<App::DocumentObject*> ObjectsListCut;
    for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
        // we need all Link objects in App::Parts for example for Assembly 4
        if ((*it)->getTypeId() == Base::Type::fromName("App::Part")) {
            App::Part* pcPart = static_cast<App::Part*>((*it));
            bool isLinkAssembly = false;
            // collect all its link objects
            auto groupObjects = pcPart->Group.getValue();
            for (auto itGO = groupObjects.begin(); itGO != groupObjects.end(); ++itGO) {
                if ((*itGO)->getTypeId() == Base::Type::fromName("App::Link")) {
                    ObjectsListCut.push_back((*itGO));
                    // we assume that App::Links inside a App::Part are an assembly
                    isLinkAssembly = true;
                }
            }
            if (isLinkAssembly) {
                // we disable the sliders because for assemblies it will takes ages to do several dozen recomputes
                QString SliderToolTip = tr("Sliders are disabled for assemblies");
                ui->cutXHS->setEnabled(false);
                ui->cutXHS->setToolTip(SliderToolTip);
                ui->cutYHS->setEnabled(false);
                ui->cutYHS->setToolTip(SliderToolTip);
                ui->cutZHS->setEnabled(false);
                ui->cutZHS->setToolTip(SliderToolTip);
            }   
        }
        // get all shapes that are also Part::Features
        if ((*it)->getPropertyByName("Shape")
            && (*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::Feature"))) {
            // sort out 2D objects, datums, App:Parts, compounds and objects that are part of a PartDesign body
            if (!(*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::Part2DObject"))
                && !(*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::Datum"))
                && !(*it)->getTypeId().isDerivedFrom(Base::Type::fromName("PartDesign::Feature"))
                && !(*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::Compound"))
                && (*it)->getTypeId() != Base::Type::fromName("App::Part"))
                ObjectsListCut.push_back((*it));
        }
    }
    
    // sort out objects that are part of Part::Boolean, Part::MultiCommon, Part::MultiFuse,
    // Part::Thickness and Part::FilletBase
    std::vector<App::DocumentObject*>::iterator it2;
    std::vector<App::DocumentObject*>::iterator it3;
    // check list of visible objects and not cut list because we want to repove from the cut list
    for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
        if ( (*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::Boolean"))
            || (*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::MultiCommon"))
            || (*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::MultiFuse"))
            || (*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::Thickness"))
            || (*it)->getTypeId().isDerivedFrom(Base::Type::fromName("Part::FilletBase")) ) {
            // get possible links
            auto subObjectList = (*it)->getOutList();
            // if there links, delete them
            if (!subObjectList.empty()) {
                for (it2 = subObjectList.begin(); it2 != subObjectList.end(); ++it2) {
                    for (it3 = ObjectsListCut.begin(); it3 != ObjectsListCut.end(); ++it3) {
                        if ((*it2) == (*it3)) {
                            ObjectsListCut.erase(it3);
                            break;
                        }
                    }
                }
            }
        }
    }

    // we might have no objects that can be cut
    if (ObjectsListCut.empty()) {
        if (isInitial)
            Base::Console().Error("SectionCut error: there are no visible objects to be cut\n");
        else
            Base::Console().Error("SectionCut error: there are no objects in the document that can be cut\n");
        // block signals to be able to reset the cut group boxes without calling startCutting again
        blockSignals(true);
        ui->groupBoxX->setChecked(false);
        ui->groupBoxY->setChecked(false);
        ui->groupBoxZ->setChecked(false);
        ui->RefreshCutPB->setEnabled(true);
        blockSignals(false);
        return;
    }
    
    // we cut this way:
    // 1. put all existing objects into a part compound
    // 2. create a box with the size of the bounding box
    // 3. cut the box from the compound
    
    // depending on how many cuts should be performed, we need as many boxes
    // if nothing is yet to be cut, we can return
    if (!ui->groupBoxX->isChecked() && !ui->groupBoxY->isChecked()
        && !ui->groupBoxZ->isChecked()) {
        // there is no active cut, so we can enable refresh button
        ui->RefreshCutPB->setEnabled(true);
        return;
    }

    // disable refresh button
    ui->RefreshCutPB->setEnabled(false);

    // create an empty compound
    auto CutCompound = doc->addObject("Part::Compound", CompoundName);
    if (!CutCompound) {
        Base::Console().Error( (std::string("SectionCut error: ")
            + std::string(CompoundName) + std::string(" could not be added\n")).c_str() );
        return;
    }
    Part::Compound* pcCompound = static_cast<Part::Compound*>(CutCompound);
    // fill it with all found elements with the copies of the elements
    int i = 0;
    for (it = ObjectsListCut.begin(), i = 0; it != ObjectsListCut.end(); ++it, i++) {
        // first create a link with a unique name
        std::string newName = (*it)->getNameInDocument();
        newName = newName + "_CutLink";
        
        auto newObject = doc->addObject("App::Link", newName.c_str());
        if (!newObject) {
            Base::Console().Error("SectionCut error: 'App::Link' could not be added\n");
            return;
        }
        App::Link* pcLink = static_cast<App::Link*>(newObject);
        // set the object to the created empty link object
        pcLink->LinkedObject.setValue((*it));
        // we want to get the link at the same position as the original
        pcLink->LinkTransform.setValue(true); 
        // add the link to the compound
        pcCompound->Links.set1Value(i, newObject);

        // hide the objects since only the cut should later be visible
        (*it)->Visibility.setValue(false);
    }

    // compute the filled compound
    pcCompound->recomputeFeature();

    // make all objects invisible so that only the compound remains
    for (it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
        (*it)->Visibility.setValue(false);
    }

    // the area in which we can cut is the size of the compound
    // we get its size by its bounding box
    SbBox3f CompoundBoundingBox = getViewBoundingBox();
    if (CompoundBoundingBox.isEmpty()) {
        Base::Console().Error("SectionCut error: the CompoundBoundingBox is empty\n");
        return;
    }

    // store the current cut positions te reset them later if possible
    double CutPosX = ui->cutX->value();
    double CutPosY = ui->cutY->value();
    double CutPosZ = ui->cutZ->value();

    // refresh all cut limits according to the new bounding box
    refreshCutRanges(CompoundBoundingBox);
        
    // prepare the cut box size according to the bounding box size
    std::vector<float> BoundingBoxSize = { 0.0, 0.0, 0.0 };
    CompoundBoundingBox.getSize(BoundingBoxSize[0], BoundingBoxSize[1], BoundingBoxSize[2]);
    // get placement of the bunding box origin
    std::vector<float> BoundingBoxOrigin = { 0.0, 0.0, 0.0 };
    CompoundBoundingBox.getOrigin(BoundingBoxOrigin[0], BoundingBoxOrigin[1], BoundingBoxOrigin[2]);

    // now we can create the cut boxes
    Base::Vector3d BoxOriginSet;
    Base::Placement placement;
    SbBox3f CutBoundingBox;
    hasBoxX = false;
    hasBoxY = false;
    hasBoxZ = false;
    hasBoxCustom = false;

    if (ui->groupBoxX->isChecked()) {
        // create a box
        auto CutBox = doc->addObject("Part::Box", BoxXName);
        if (!CutBox) {
            Base::Console().Error( (std::string("SectionCut error: ")
                + std::string(BoxXName) + std::string(" could not be added\n")).c_str() );
            return;
        }
        Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
        // it appears that because of internal rounding errors, the bounding box is sometimes
        // a bit too small, for example for epplipsoides, thus make the box a bit larger
        pcBox->Length.setValue(BoundingBoxSize[0] + 1.0);
        pcBox->Width.setValue(BoundingBoxSize[1] + 1.0);
        pcBox->Height.setValue(BoundingBoxSize[2] + 1.0);
        // set the previous cut value because refreshCutRanges changed it
        // in case the there was previously no cut, nothing will actually be changed
        // the previous value might now be outside the current possible range, then reset it
        if (CutPosX >= ui->cutX->maximum()) {
            CutPosX = ui->cutX->maximum() - 0.1; // short below the maximum
        }
        else if (CutPosX <= ui->cutX->minimum()) {
            CutPosX = ui->cutX->minimum() + 0.1; // short above the minimum
        }
        // we don't set the value to ui->cutX because this would refresh the cut
        // which we don't have yet, thus do this later
        //set the box position
        if (!ui->flipX->isChecked())
            BoxOriginSet.x = CutPosX - (BoundingBoxSize[0] + 1.0);
        else //flipped
            BoxOriginSet.x = CutPosX;
        // we made the box 1.0 larger that we can place it 0.5 below the bounding box
        BoxOriginSet.y = BoundingBoxOrigin[1] - 0.5;
        BoxOriginSet.z = BoundingBoxOrigin[2] - 0.5;
        placement.setPosition(BoxOriginSet);
        pcBox->Placement.setValue(placement);

        // create a cut feature
        auto CutFeature = doc->addObject("Part::Cut", CutXName);
        if (!CutFeature) {
            Base::Console().Error( (std::string("SectionCut error: ")
                + std::string(CutXName) + std::string(" could not be added\n")).c_str() );
            return;
        }
        Part::Cut* pcCut = static_cast<Part::Cut*>(CutFeature);
        pcCut->Base.setValue(CutCompound);
        pcCut->Tool.setValue(CutBox);

        // set the cut value
        ui->cutX->setValue(CutPosX);
        // recomputing recursively is especially for assemblies very time-consuming
        // however there must be a final recursicve recompute and we do this at the end
        // so only recomute recursively if there are no other cuts
        if (!ui->groupBoxY->isChecked() && !ui->groupBoxZ->isChecked())
            pcCut->recomputeFeature(true);
        else
            pcCut->recomputeFeature(false);
        hasBoxX = true;
    }
    if (ui->groupBoxY->isChecked()) {
        // if there is a X cut, its size defines the possible range for the Y cut
        // the cut box size is not affected, it can be as large as the compound
        if (hasBoxX) {
            CutBoundingBox = getViewBoundingBox();
            // refresh the Y cut limits according to the new bounding box
            refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
                Refresh::notXRange, Refresh::YRange, Refresh::notZRange);
        }
        auto CutBox = doc->addObject("Part::Box", BoxYName);
        if (!CutBox) {
            Base::Console().Error((std::string("SectionCut error: ")
                + std::string(BoxYName) + std::string(" could not be added\n")).c_str() );
            return;
        }
        Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
        pcBox->Length.setValue(BoundingBoxSize[0] + 1.0);
        pcBox->Width.setValue(BoundingBoxSize[1] + 1.0);
        pcBox->Height.setValue(BoundingBoxSize[2] + 1.0);
        // reset previous cut value
        if (CutPosY >= ui->cutY->maximum()) {
            CutPosY = ui->cutY->maximum() - 0.1; // short below the maximum
        }
        else if (CutPosY <= ui->cutY->minimum()) {
            CutPosY = ui->cutY->minimum() + 0.1; // short above the minimum
        }
        //set the box position
        BoxOriginSet.x = BoundingBoxOrigin[0] - 0.5;
        if (!ui->flipY->isChecked())
            BoxOriginSet.y = CutPosY - (BoundingBoxSize[1] + 1.0);
        else //flipped
            BoxOriginSet.y = CutPosY;
        BoxOriginSet.z = BoundingBoxOrigin[2] - 0.5;
        placement.setPosition(BoxOriginSet);
        pcBox->Placement.setValue(placement);
        
        auto CutFeature = doc->addObject("Part::Cut", CutYName);
        if (!CutFeature) {
            Base::Console().Error((std::string("SectionCut error: ")
                + std::string(CutYName) + std::string(" could not be added\n")).c_str() );
            return;
        }
        Part::Cut* pcCut = static_cast<Part::Cut*>(CutFeature);
        // if there is already a cut, we must take it as feature to be cut
        if (hasBoxX)
            pcCut->Base.setValue(doc->getObject(CutXName));
        else
            pcCut->Base.setValue(CutCompound);
        pcCut->Tool.setValue(CutBox);
        
        // set the cut value
        ui->cutY->setValue(CutPosY);
        if (!ui->groupBoxZ->isChecked())
            pcCut->recomputeFeature(true);
        else
            pcCut->recomputeFeature(false);
        hasBoxY = true;
    }
    if (ui->groupBoxZ->isChecked()) {
        if (hasBoxX || hasBoxY) {
            CutBoundingBox = getViewBoundingBox();
            refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
                Refresh::notXRange, Refresh::notYRange, Refresh::ZRange);
        }
        auto CutBox = doc->addObject("Part::Box", BoxZName);
        if (!CutBox) {
            Base::Console().Error((std::string("SectionCut error: ")
                + std::string(BoxZName) + std::string(" could not be added\n")).c_str() );
            return;
        }
        Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
        pcBox->Length.setValue(BoundingBoxSize[0] + 1.0);
        pcBox->Width.setValue(BoundingBoxSize[1] + 1.0);
        pcBox->Height.setValue(BoundingBoxSize[2] + 1.0);
        // reset previous cut value
        if (CutPosZ >= ui->cutZ->maximum()) {
            CutPosZ = ui->cutZ->maximum() - 0.1; // short below the maximum
        }
        else if (CutPosZ <= ui->cutZ->minimum()) {
            CutPosZ = ui->cutZ->minimum() + 0.1; // short above the minimum
        }
        //set the box position
        BoxOriginSet.x = BoundingBoxOrigin[0] - 0.5;
        BoxOriginSet.y = BoundingBoxOrigin[1] - 0.5;
        if (!ui->flipY->isChecked())
            BoxOriginSet.z = CutPosZ - (BoundingBoxSize[2] + 1.0);
        else //flipped
            BoxOriginSet.z = CutPosZ;
        placement.setPosition(BoxOriginSet);
        pcBox->Placement.setValue(placement);

        auto CutFeature = doc->addObject("Part::Cut", CutZName);
        if (!CutFeature) {
            Base::Console().Error( (std::string("SectionCut error: ")
                + std::string(CutZName) + std::string(" could not be added\n")).c_str() );
            return;
        }
        Part::Cut* pcCut = static_cast<Part::Cut*>(CutFeature);
        // if there is already a cut, we must take it as feature to be cut
        if (hasBoxY) {
            pcCut->Base.setValue(doc->getObject(CutYName));
        }
        else if (hasBoxX && !hasBoxY) {
            pcCut->Base.setValue(doc->getObject(CutXName));
        }
        else {
            pcCut->Base.setValue(CutCompound);
        }
        pcCut->Tool.setValue(CutBox);

        // set the cut value
        ui->cutZ->setValue(CutPosZ);
        pcCut->recomputeFeature(true);
        hasBoxZ = true;
    }
}

SectionCut* SectionCut::makeDockWidget(Gui::View3DInventor* view)
{
    // embed this dialog into a QDockWidget
    SectionCut* sectionCut = new SectionCut(view);
    Gui::DockWindowManager* pDockMgr = Gui::DockWindowManager::instance();
    // the dialog is designed that you can see the tree, thus put it to the right side
    QDockWidget* dw = pDockMgr->addDockWindow("Section Cutting", sectionCut, Qt::RightDockWidgetArea);
    dw->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    //dw->setFloating(true);
    dw->show();

    return sectionCut;
}

/** Destroys the object and frees any allocated resources */
SectionCut::~SectionCut()
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    if (!ui->keepOnlyCutCB->isChecked()) {
        // make all objects visible that have been visible when the dialog was called
        // because we made them invisible when we created cuts
        for (auto it = ObjectsListVisible.begin(); it != ObjectsListVisible.end(); ++it) {
            if ((*it)->isValid()) // a formerly visible object might have been deleted
                (*it)->Visibility.setValue(true);
        }
    }
}

void SectionCut::reject()
{
    QDialog::reject();
    QDockWidget* dw = qobject_cast<QDockWidget*>(parent());
    if (dw) {
        dw->deleteLater();
    }
}

void SectionCut::onGroupBoxXtoggled()
{
    // reset the cut
    startCutting();
}

void SectionCut::onGroupBoxYtoggled()
{
    startCutting();
}

void SectionCut::onGroupBoxZtoggled()
{
    startCutting();
}

void SectionCut::onCutXvalueChanged(double val)
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    // refresh objects and return in case the document was changed
    if (doc != Gui::Application::Instance->activeDocument()->getDocument()) {
        onRefreshCutPBclicked();
        return;
    }
    // update slider position and tooltip
    // the slider value is % of the cut range
    ui->cutXHS->setValue(
        int((val - ui->cutX->minimum())
            / (ui->cutX->maximum() - ui->cutX->minimum()) * 100.0));
    ui->cutXHS->setToolTip(QString::number(val, 'g', Base::UnitsApi::getDecimals()));

    // we cannot cut to the edge because then the result is an empty shape
        // we chose purposely not to simply set the range for cutX previously
        // because everything is allowed just not the min/max
    if (ui->cutX->value() == ui->cutX->maximum()) {
        ui->cutX->setValue(ui->cutX->maximum() - 0.1);
        return;
    }
    if (ui->cutX->value() == ui->cutX->minimum()) {
        ui->cutX->setValue(ui->cutX->minimum() + 0.1);
        return;
    }
    // get the cut box
    auto CutBox = doc->getObject(BoxXName);
    // when the value has been set after resetting the compound bounding box
    // there is not yet a cut and we do nothing
    if (!CutBox)
        return;
    Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
    // get its placement and size
    Base::Placement placement = pcBox->Placement.getValue();
    Base::Vector3d BoxPosition = placement.getPosition();
    // change the placement
    if (!ui->flipX->isChecked())
        BoxPosition.x = ui->cutX->value() - pcBox->Length.getValue();
    else //flipped
        BoxPosition.x = ui->cutX->value();
    placement.setPosition(BoxPosition);
    pcBox->Placement.setValue(placement);

    auto CutObject = doc->getObject(CutXName);
    // there should be a box, but maybe the user deleted it meanwhile
    if (!CutObject) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(CutXName) + std::string(", trying to recreate it\n")).c_str());
        // recreate the box
        startCutting();
        return;
    }

    // if there is another cut, we must recalculate it too
    // we might have cut so that the range for Y and Z is now smaller
    // the hierarchy is always Z->Y->X
    if (hasBoxY && !hasBoxZ) { // only Y
        auto CutFeatureY = doc->getObject(CutYName);
        if (!CutFeatureY) {
            Base::Console().Warning((std::string("SectionCut warning: there is no ")
                + std::string(CutYName) + std::string(", trying to recreate it\n")).c_str());
            startCutting();
            return;
        }
        // refresh the Y and Z cut limits according to the new bounding box of the cut result
        // make the SectionCutY invisible
        CutFeatureY->Visibility.setValue(false);
        // make SectionCutX visible
        CutObject->Visibility.setValue(true);
        // get new bounding box
        auto CutBoundingBox = getViewBoundingBox();
        // refresh Y limits and Z limits + Z value
        refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::ZValue,
            Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
        // the value of Y can now be outside or at the limit, in this case reset the value too
        if ((ui->cutY->value() >= ui->cutY->maximum())
            || (ui->cutY->value() <= ui->cutY->minimum()))
            refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::YValue, Refresh::ZValue,
                Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
        // make the SectionCutY visible again
        CutFeatureY->Visibility.setValue(true);
        // make SectionCutX invisible again
        CutObject->Visibility.setValue(false);
        // recompute the cut
        Part::Cut* pcCutY = static_cast<Part::Cut*>(CutFeatureY);
        pcCutY->recomputeFeature(true);
    }
    else if (hasBoxZ) { // at least Z
        // the main cut is Z, no matter if there is a cut in Y
        auto CutFeatureZ = doc->getObject(CutZName);
        if (!CutFeatureZ) {
            Base::Console().Error((std::string("SectionCut error: there is no ")
                + std::string(CutZName) + std::string("\n")).c_str());
            return;
        }
        // refresh the Y and Z cut limits according to the new bounding box of the cut result
        // make the SectionCutZ invisible
        CutFeatureZ->Visibility.setValue(false);
        // make SectionCutX visible
        CutObject->Visibility.setValue(true);
        // get new bounding box
        auto CutBoundingBox = getViewBoundingBox();
        // refresh Y and Z limits
        if (hasBoxY) {
            refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
                Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
            // the value of Y or Z can now be outside or at the limit, in this case reset the value too
            if ((ui->cutY->value() >= ui->cutY->maximum())
                || (ui->cutY->value() <= ui->cutY->minimum()))
                refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::YValue, Refresh::notZValue,
                    Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
            if ((ui->cutZ->value() >= ui->cutZ->maximum())
                || (ui->cutZ->value() <= ui->cutZ->minimum()))
                refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::ZValue,
                    Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
        }
        else { // there is no Y cut yet so we can set the Y value too
            refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::YValue, Refresh::notZValue,
                Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
            // the value of Z can now be outside or at the limit, in this case reset the value too
            if ((ui->cutZ->value() >= ui->cutZ->maximum())
                || (ui->cutZ->value() <= ui->cutZ->minimum()))
                refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::YValue, Refresh::ZValue,
                    Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
        }
        // make the SectionCutZ visible again
        CutFeatureZ->Visibility.setValue(true);
        // make SectionCutX invisible again
        CutObject->Visibility.setValue(false);
        // recompute the cut
        Part::Cut* pcCutZ = static_cast<Part::Cut*>(CutFeatureZ);
        pcCutZ->recomputeFeature(true);
    }
    else { // just X
        // refresh Y and Z limits + values
        auto CutBoundingBox = getViewBoundingBox();
        refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::YValue, Refresh::ZValue,
            Refresh::notXRange, Refresh::YRange, Refresh::ZRange);
        // recompute the cut
        Part::Cut* pcCut = static_cast<Part::Cut*>(CutObject);
        pcCut->recomputeFeature(true);
    }
}

void SectionCut::onCutXHSsliderMoved(int val)
{
    // we cannot cut to the edge because then the result is an empty shape
    // we chose purposely not to simply set the range for cutXHS previously
    // because everything is allowed just not the min/max
    // we set it one slider step below the min/max
    if (val == ui->cutXHS->maximum()) {
        ui->cutXHS->setValue(ui->cutXHS->maximum() - ui->cutXHS->singleStep());
        return;
    }
    if (val == ui->cutXHS->minimum()) {
        ui->cutXHS->setValue(ui->cutXHS->minimum() + ui->cutXHS->singleStep());
        return;
    }
    // the slider value is % of the cut range
    double NewCutValue = ui->cutX->minimum()
        + val / 100.0 * (ui->cutX->maximum() - ui->cutX->minimum());
    ui->cutXHS->setToolTip(QString::number(NewCutValue, 'g', Base::UnitsApi::getDecimals()));
    ui->cutX->setValue(NewCutValue);
}

void SectionCut::onCutYvalueChanged(double val)
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    // refresh objects and return in case the document was changed
    if (doc != Gui::Application::Instance->activeDocument()->getDocument()) {
        onRefreshCutPBclicked();
        return;
    }
    // update slider position and tooltip
    // the slider value is % of the cut range
    ui->cutYHS->setValue(
        int((val - ui->cutY->minimum())
            / (ui->cutY->maximum() - ui->cutY->minimum()) * 100.0));
    ui->cutYHS->setToolTip(QString::number(val, 'g', Base::UnitsApi::getDecimals()));

    // we cannot cut to the edge because then the result is an empty shape
    if (ui->cutY->value() == ui->cutY->maximum()) {
        ui->cutY->setValue(ui->cutY->maximum() - 0.1);
        return;
    }
    if (ui->cutY->value() == ui->cutY->minimum()) {
        ui->cutY->setValue(ui->cutY->minimum() + 0.1);
        return;
    }
    auto CutBox = doc->getObject(BoxYName);
    if (!CutBox)
        return;
    Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
    Base::Placement placement = pcBox->Placement.getValue();
    Base::Vector3d BoxPosition = placement.getPosition();
    if (!ui->flipY->isChecked())
        BoxPosition.y = ui->cutY->value() - pcBox->Width.getValue();
    else //flipped
        BoxPosition.y = ui->cutY->value();
    placement.setPosition(BoxPosition);
    pcBox->Placement.setValue(placement);

    auto CutObject = doc->getObject(CutYName);
    if (!CutObject) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(CutYName) + std::string(", trying to recreate it\n")).c_str());
        startCutting();
        return;
    }

    // if there is another cut, we must recalculate it too
    // we might have cut so that the range for Z is now smaller
    // we only need to check for Z since the hierarchy is always Z->Y->X
    if (hasBoxZ) {
        auto CutFeatureZ = doc->getObject(CutZName);
        if (!CutFeatureZ) {
            Base::Console().Error((std::string("SectionCut error: there is no ")
                + std::string(CutZName) + std::string("\n")).c_str());
            return;
        }
        // refresh the Z cut limits according to the new bounding box of the cut result
        // make the SectionCutZ invisible
        CutFeatureZ->Visibility.setValue(false);
        // make SectionCutX visible
        CutObject->Visibility.setValue(true);
        // get new bunding box
        auto CutBoundingBox = getViewBoundingBox();
        // refresh Z limits
        refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
            Refresh::notXRange, Refresh::notYRange, Refresh::ZRange);
        // the value of Z can now be outside or at the limit, in this case reset the value too
        if ((ui->cutZ->value() >= ui->cutZ->maximum())
            || (ui->cutZ->value() <= ui->cutZ->minimum()))
            refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::ZValue,
                Refresh::notXRange, Refresh::notYRange, Refresh::ZRange);
        // make the SectionCutZ visible again
        CutFeatureZ->Visibility.setValue(true);
        // make SectionCutX invisible again
        CutObject->Visibility.setValue(false);
        // recompute the cut
        Part::Cut* pcCutZ = static_cast<Part::Cut*>(CutFeatureZ);
        pcCutZ->recomputeFeature(true);
    }
    else { // just Y
        // refresh Z limits + values
        auto CutBoundingBox = getViewBoundingBox();
        refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::ZValue,
            Refresh::notXRange, Refresh::notYRange, Refresh::ZRange);
        // recompute the cut
        Part::Cut* pcCut = static_cast<Part::Cut*>(CutObject);
        pcCut->recomputeFeature(true);
        // refresh X limits
        // this is done by
        // first making the cut X box visible, the setting the limits only for X
        // if x-limit in box direcion is larger than object, reset value to saved limit
        if (hasBoxX) {
            auto CutBoxX = doc->getObject(BoxXName);
            if (!CutBoxX)
                return;
            // first store the values
            double storedX;
            if (!ui->flipX->isChecked())
                storedX = ui->cutX->minimum();
            else
                storedX = ui->cutX->maximum();
            // show the cutting box
            CutBoxX->Visibility.setValue(true);
            // set new XRange
            auto CutBoundingBox = getViewBoundingBox();
            refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
                Refresh::XRange, Refresh::notYRange, Refresh::notZRange);
            // hide cutting box and compare resultwith stored value
            CutBoxX->Visibility.setValue(false);
            if (!ui->flipX->isChecked()) {
                if (storedX > ui->cutX->minimum())
                    ui->cutX->setMinimum(storedX);
            }
            else {
                if (storedX < ui->cutX->maximum())
                    ui->cutX->setMaximum(storedX);
            }
        }
    }
}

void SectionCut::onCutYHSsliderMoved(int val)
{
    // we cannot cut to the edge because then the result is an empty shape
    if (val == ui->cutYHS->maximum()) {
        ui->cutYHS->setValue(ui->cutYHS->maximum() - ui->cutYHS->singleStep());
        return;
    }
    if (val == ui->cutYHS->minimum()) {
        ui->cutYHS->setValue(ui->cutYHS->minimum() + ui->cutYHS->singleStep());
        return;
    }
    // the slider value is % of the cut range
    double NewCutValue = ui->cutY->minimum()
        + val / 100.0 * (ui->cutY->maximum() - ui->cutY->minimum());
    ui->cutYHS->setToolTip(QString::number(NewCutValue, 'g', Base::UnitsApi::getDecimals()));
    ui->cutY->setValue(NewCutValue);
}

void SectionCut::onCutZvalueChanged(double val)
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    // refresh objects and return in case the document was changed
    if (doc != Gui::Application::Instance->activeDocument()->getDocument()) {
        onRefreshCutPBclicked();
        return;
    }
    // update slider position and tooltip
    // the slider value is % of the cut range
    ui->cutZHS->setValue(
        int((val - ui->cutZ->minimum())
            / (ui->cutZ->maximum() - ui->cutZ->minimum()) * 100.0));
    ui->cutZHS->setToolTip(QString::number(val, 'g', Base::UnitsApi::getDecimals()));

    // we cannot cut to the edge because then the result is an empty shape
    if (ui->cutZ->value() == ui->cutZ->maximum()) {
        ui->cutZ->setValue(ui->cutZ->maximum() - 0.1);
        return;
    }
    if (ui->cutZ->value() == ui->cutZ->minimum()) {
        ui->cutZ->setValue(ui->cutZ->minimum() + 0.1);
        return;
    }
    auto CutBox = doc->getObject(BoxZName);
    if (!CutBox)
        return;
    Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
    Base::Placement placement = pcBox->Placement.getValue();
    Base::Vector3d BoxPosition = placement.getPosition();
    if (!ui->flipZ->isChecked())
        BoxPosition.z = ui->cutZ->value() - pcBox->Height.getValue();
    else //flipped
        BoxPosition.z = ui->cutZ->value();
    placement.setPosition(BoxPosition);
    pcBox->Placement.setValue(placement);

    auto CutObject = doc->getObject(CutZName);
    if (!CutObject) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(CutZName) + std::string(", trying to recreate it\n")).c_str());
        startCutting();
        return;
    }
    Part::Cut* pcCut = static_cast<Part::Cut*>(CutObject);
    pcCut->recomputeFeature(true);
    // refresh X and Y limits
    // this is done e.g. for X by
    // first making the cut X box visible, the setting the limits only for X
    // if x-limit in box direcion is larger than object, reset value to saved limit
    SbBox3f CutBoundingBox;
    if (hasBoxX) {
        auto CutBoxX = doc->getObject(BoxXName);
        if (!CutBoxX)
            return;
        // first store the values
        double storedX;
        if (!ui->flipX->isChecked())
            storedX = ui->cutX->minimum();
        else
            storedX = ui->cutX->maximum();
        // show the cutting box
        CutBoxX->Visibility.setValue(true);
        // set new XRange
        CutBoundingBox = getViewBoundingBox();
        refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
            Refresh::XRange, Refresh::notYRange, Refresh::notZRange);
        // hide cutting box and compare resultwith stored value
        CutBoxX->Visibility.setValue(false);
        if (!ui->flipX->isChecked()) {
            if (storedX > ui->cutX->minimum())
                ui->cutX->setMinimum(storedX);
        }
        else {
            if (storedX < ui->cutX->maximum())
                ui->cutX->setMaximum(storedX);
        }
    }
    if (hasBoxY) {
        auto CutBoxY = doc->getObject(BoxYName);
        if (!CutBoxY)
            return;
        double storedY;
        if (!ui->flipY->isChecked())
            storedY = ui->cutY->minimum();
        else
            storedY = ui->cutY->maximum();
        CutBoxY->Visibility.setValue(true);
        CutBoundingBox = getViewBoundingBox();
        refreshCutRanges(CutBoundingBox, Refresh::notXValue, Refresh::notYValue, Refresh::notZValue,
            Refresh::notXRange, Refresh::YRange, Refresh::notZRange);
        CutBoxY->Visibility.setValue(false);
        if (!ui->flipY->isChecked()) {
            if (storedY > ui->cutY->minimum())
                ui->cutY->setMinimum(storedY);
        }
        else {
            if (storedY < ui->cutY->maximum())
                ui->cutY->setMaximum(storedY);
        }
    }
}

void SectionCut::onCutZHSsliderMoved(int val)
{
    // we cannot cut to the edge because then the result is an empty shape
    if (val == ui->cutZHS->maximum()) {
        ui->cutZHS->setValue(ui->cutZHS->maximum() - ui->cutZHS->singleStep());
        return;
    }
    if (val == ui->cutZHS->minimum()) {
        ui->cutZHS->setValue(ui->cutZHS->minimum() + ui->cutZHS->singleStep());
        return;
    }
    // the slider value is % of the cut range
    double NewCutValue = ui->cutZ->minimum()
        + val / 100.0 * (ui->cutZ->maximum() - ui->cutZ->minimum());
    ui->cutZHS->setToolTip(QString::number(NewCutValue, 'g', Base::UnitsApi::getDecimals()));
    ui->cutZ->setValue(NewCutValue);
}

void SectionCut::onFlipXclicked()
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    // refresh objects and return in case the document was changed
    if (doc != Gui::Application::Instance->activeDocument()->getDocument()) {
        onRefreshCutPBclicked();
        return;
    }
    // we must move the box in x-direction by its Length
    // get the cut box
    auto CutBox = doc->getObject(BoxXName);
    // there should be a box, but maybe the user deleted it meanwhile
    if (!CutBox) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(BoxXName) + std::string(", trying to recreate it\n")).c_str());
        // recreate the box
        startCutting();
        return;
    }
    Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
    // get its placement and size
    Base::Placement placement = pcBox->Placement.getValue();
    Base::Vector3d BoxPosition = placement.getPosition();
    // flip the box
    if (ui->flipX->isChecked())
        BoxPosition.x = BoxPosition.x + pcBox->Length.getValue();
    else
        BoxPosition.x = BoxPosition.x - pcBox->Length.getValue();
    placement.setPosition(BoxPosition);
    pcBox->Placement.setValue(placement);

    auto CutObject = doc->getObject(CutXName);
    // there should be a cut, but maybe the user deleted it meanwhile
    if (!CutObject) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(CutXName) + std::string(", trying to recreate it\n")).c_str());
        // recreate the box
        startCutting();
        return;
    }

    // if there is another cut, we must recalculate it too
    // the hierarchy is always Z->Y->X
    if (hasBoxY && !hasBoxZ) { // only Y
        auto CutFeatureY = doc->getObject(CutYName);
        if (!CutFeatureY) {
            Base::Console().Warning(
                (std::string("SectionCut warning: the expected ")
                    + std::string(CutYName) + std::string(" is missing, trying to recreate it\n")).c_str());
            // recreate the box
            startCutting();
            return;
        }
        Part::Cut* pcCutY = static_cast<Part::Cut*>(CutFeatureY);
        pcCutY->recomputeFeature(true);
    }
    else if ((!hasBoxY && hasBoxZ) || (hasBoxY && hasBoxZ)) { // at least Z
        // the main cut is Z, no matter if there is a cut in Y
        auto CutFeatureZ = doc->getObject(CutZName);
        if (!CutFeatureZ) {
            Base::Console().Warning((std::string("SectionCut warning: the expected ")
                + std::string(CutZName) + std::string(" is missing, trying to recreate it\n")).c_str());
            // recreate the box
            startCutting();
            return;
        }
        Part::Cut* pcCutZ = static_cast<Part::Cut*>(CutFeatureZ);
        pcCutZ->recomputeFeature(true);
    }
    else { // only do this when no other box to save recomputes
        Part::Cut* pcCut = static_cast<Part::Cut*>(CutObject);
        pcCut->recomputeFeature(true);
    }     
}

void SectionCut::onFlipYclicked()
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    // refresh objects and return in case the document was changed
    if (doc != Gui::Application::Instance->activeDocument()->getDocument()) {
        onRefreshCutPBclicked();
        return;
    }
    // we must move the box in y-direction by its Width
        // get the cut box
    auto CutBox = doc->getObject(BoxYName);
    // there should be a box, but maybe the user deleted it meanwhile
    if (!CutBox) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(BoxYName) + std::string(", trying to recreate it\n")).c_str());
        // recreate the box
        startCutting();
        return;
    }
    Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
    // get its placement and size
    Base::Placement placement = pcBox->Placement.getValue();
    Base::Vector3d BoxPosition = placement.getPosition();
    // flip the box
    if (ui->flipY->isChecked())
        BoxPosition.y = BoxPosition.y + pcBox->Width.getValue();
    else
        BoxPosition.y = BoxPosition.y - pcBox->Width.getValue();
    placement.setPosition(BoxPosition);
    pcBox->Placement.setValue(placement);

    auto CutObject = doc->getObject(CutYName);
    // there should be a cut, but maybe the user deleted it meanwhile
    if (!CutObject) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(CutYName) + std::string(", trying to recreate it\n")).c_str());
        // recreate the box
        startCutting();
        return;
    }

    // if there is another cut, we must recalculate it too
    // we only need to check for Z since the hierarchy is always Z->Y->X
    if (hasBoxZ) {
        auto CutFeatureZ = doc->getObject(CutZName);
        Part::Cut* pcCutZ = static_cast<Part::Cut*>(CutFeatureZ);
        pcCutZ->recomputeFeature(true);
    }
    else {
        Part::Cut* pcCut = static_cast<Part::Cut*>(CutObject);
        pcCut->recomputeFeature(true);
    }
}

void SectionCut::onFlipZclicked()
{
    // there might be no document
    if (!Gui::Application::Instance->activeDocument()) {
        noDocumentActions();
        return;
    }
    // refresh objects and return in case the document was changed
    if (doc != Gui::Application::Instance->activeDocument()->getDocument()) {
        onRefreshCutPBclicked();
        return;
    }
    // we must move the box in z-direction by its Height
        // get the cut box
    auto CutBox = doc->getObject(BoxZName);
    // there should be a box, but maybe the user deleted it meanwhile
    if (!CutBox) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(BoxZName) + std::string(", trying to recreate it\n")).c_str());
        // recreate the box
        startCutting();
        return;
    }
    Part::Box* pcBox = static_cast<Part::Box*>(CutBox);
    // get its placement and size
    Base::Placement placement = pcBox->Placement.getValue();
    Base::Vector3d BoxPosition = placement.getPosition();
    // flip the box
    if (ui->flipZ->isChecked())
        BoxPosition.z = BoxPosition.z + pcBox->Height.getValue();
    else
        BoxPosition.z = BoxPosition.z - pcBox->Height.getValue();
    placement.setPosition(BoxPosition);
    pcBox->Placement.setValue(placement);

    auto CutObject = doc->getObject(CutZName);
    // there should be a cut, but maybe the user deleted it meanwhile
    if (!CutObject) {
        Base::Console().Warning((std::string("SectionCut warning: there is no ")
            + std::string(CutZName) + std::string(", trying to recreate it\n")).c_str());
        // recreate the box
        startCutting();
        return;
    }
    Part::Cut* pcCut = static_cast<Part::Cut*>(CutObject);
    pcCut->recomputeFeature(true);
}

// refreshes the list of document objects and the visible objects
void SectionCut::onRefreshCutPBclicked()
{
    // get document
    doc = Gui::Application::Instance->activeDocument()->getDocument();
    if (!doc) {
        Base::Console().Error("SectionCut error: there is no document\n");
        return;
    }
    // get all objects in the document
    std::vector<App::DocumentObject*> ObjectsList = doc->getObjects();
    if (ObjectsList.empty()) {
        Base::Console().Error("SectionCut error: there are no objects in the document\n");
        return;
    }
    // empty the ObjectsListVisible
    ObjectsListVisible.clear();
    // now store those that are currently visible
    for (auto it = ObjectsList.begin(); it != ObjectsList.end(); ++it) {
        if ((*it)->Visibility.getValue())
            ObjectsListVisible.push_back((*it));
    }
    // reset defaults
    hasBoxX = false;
    hasBoxY = false;
    hasBoxZ = false;
    // we can have existing cuts
    if (doc->getObject(CutZName)) {
        hasBoxZ = true;
        blockSignals(true);
        ui->groupBoxZ->setChecked(true);
        blockSignals(false);
    }
    if (doc->getObject(CutYName)) {
        hasBoxY = true;
        blockSignals(true);
        ui->groupBoxY->setChecked(true);
        blockSignals(false);
    }
    if (doc->getObject(CutXName)) {
        hasBoxX = true;
        blockSignals(true);
        ui->groupBoxX->setChecked(true);
        blockSignals(false);
    }
    // if there is a cut, disable the button
    if (hasBoxX || hasBoxY || hasBoxZ)
        ui->RefreshCutPB->setEnabled(false);
}

SbBox3f SectionCut::getViewBoundingBox()
{
    auto docGui = Gui::Application::Instance->activeDocument();
    Gui::View3DInventor* view = static_cast<Gui::View3DInventor*>(docGui->getActiveView());
    Gui::View3DInventorViewer* viewer = view->getViewer();
    SoCamera* camera = viewer->getSoRenderManager()->getCamera();
    SbBox3f Box;
    if (!camera || !camera->isOfType(SoOrthographicCamera::getClassTypeId()))
        return Box; // return an empty box
    // get scene bounding box
    SoGetBoundingBoxAction action(viewer->getSoRenderManager()->getViewportRegion());
    action.apply(viewer->getSceneGraph());
    return action.getBoundingBox();
}

void SectionCut::refreshCutRanges(SbBox3f BoundingBox,
    bool forXValue, bool forYValue, bool forZValue,
    bool forXRange, bool forYRange, bool forZRange)
{
    if (!BoundingBox.isEmpty()) {
        SbVec3f cnt = BoundingBox.getCenter();
        int minDecimals = Base::UnitsApi::getDecimals();
        float lenx, leny, lenz;
        BoundingBox.getSize(lenx, leny, lenz);
        int steps = 100;

        // set the ranges
        float rangeMin; // to silence a compiler warning we use a float
        float rangeMax;
        if (forXRange) {
            rangeMin = cnt[0] - (lenx / 2);
            rangeMax = cnt[0] + (lenx / 2);
            ui->cutX->setRange(rangeMin, rangeMax);
            // determine the single step values
            lenx = lenx / steps;
            int dim = static_cast<int>(log10(lenx));
            double singleStep = pow(10.0, dim);
            ui->cutX->setSingleStep(singleStep);
        }
        if (forYRange) {
            rangeMin = cnt[1] - (leny / 2);
            rangeMax = cnt[1] + (leny / 2);
            ui->cutY->setRange(rangeMin, rangeMax);
            leny = leny / steps;
            int dim = static_cast<int>(log10(leny));
            double singleStep = pow(10.0, dim);
            ui->cutY->setSingleStep(singleStep);
        }
        if (forZRange) {
            rangeMin = cnt[2] - (lenz / 2);
            rangeMax = cnt[2] + (lenz / 2);
            ui->cutZ->setRange(rangeMin, rangeMax);
            lenz = lenz / steps;
            int dim = static_cast<int>(log10(lenz));
            double singleStep = pow(10.0, dim);
            ui->cutZ->setSingleStep(singleStep);
        }
        if (forXValue) {
            ui->cutX->setValue(cnt[0]);
            ui->cutXHS->setValue(50);
        }
        if (forYValue) {
            ui->cutY->setValue(cnt[1]);
            ui->cutYHS->setValue(50);
        }
        if (forZValue) {
            ui->cutZ->setValue(cnt[2]);
            ui->cutZHS->setValue(50);
        }

        // set decimals
        ui->cutX->setDecimals(minDecimals);
        ui->cutY->setDecimals(minDecimals);
        ui->cutZ->setDecimals(minDecimals);
    }
}

#include "moc_SectionCutting.cpp"