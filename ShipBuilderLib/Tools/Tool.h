/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2021-09-12
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "IUserInterface.h"
#include "ModelController.h"
#include "ShipBuilderTypes.h"
#include "UndoStack.h"
#include "WorkbenchState.h"
#include "View.h"

#include <GameCore/GameTypes.h>

namespace ShipBuilder {

/*
 * Base class for all tools.
 *
 * Tools:
 * - "Extensions" of Controller
 * - Implement state machines for interactions, including visual notifications (marching ants, paste mask, etc.)
 * - Take references to WorkbenchState and SelectionManager (at tool initialization time)
 *     - SelectionManager so that Selection tool can save selection to it
 * - Receive input state events from Controller, and notifications of WorkbenchState changed
 * - Take references to View and ModelController
 * - Modify Model via ModelController
 * - Instruct View for tool interactions, e.g. tool visualizations (lines, paste mask, etc.)
 * - Have also reference to IUserInterface, e.g. to capture/release mouse
 */
class Tool
{
public:

    virtual ~Tool() = default;

    ToolType GetType() const
    {
        return mToolType;
    }

    //
    // Event handlers
    //

    virtual void OnMouseMove(ShipSpaceCoordinates const & mouseCoordinates) = 0;
    virtual void OnLeftMouseDown() = 0;
    virtual void OnLeftMouseUp() = 0;
    virtual void OnRightMouseDown() = 0;
    virtual void OnRightMouseUp() = 0;
    virtual void OnShiftKeyDown() = 0;
    virtual void OnShiftKeyUp() = 0;

protected:

    Tool(
        ToolType toolType,
        ModelController & modelController,
        UndoStack & undoStack,
        WorkbenchState const & workbenchState,
        IUserInterface & userInterface,
        View & view)
        : mToolType(toolType)
        , mModelController(modelController)
        , mUndoStack(undoStack)
        , mWorkbenchState(workbenchState)
        , mUserInterface(userInterface)
        , mView(view)
    {}

    void SetCursor(wxImage const & cursorImage)
    {
        mUserInterface.SetToolCursor(cursorImage);
    }

    void ScrollIntoViewIfNeeded(DisplayLogicalCoordinates const & mouseScreenPosition)
    {
        mUserInterface.ScrollIntoViewIfNeeded(mouseScreenPosition);
    }

    void SetLayerDirty(LayerType layer)
    {
        mModelController.SetLayerDirty(layer);
        mUserInterface.OnModelDirtyChanged();
    }

    void PushUndoAction(std::unique_ptr<UndoAction> && undoAction)
    {
        mUndoStack.Push(std::move(undoAction));
        mUserInterface.OnUndoStackStateChanged();
    }

protected:

    ToolType const mToolType;

    ModelController & mModelController;
    UndoStack & mUndoStack;
    WorkbenchState const & mWorkbenchState;
    IUserInterface & mUserInterface;
    View & mView;
};

}