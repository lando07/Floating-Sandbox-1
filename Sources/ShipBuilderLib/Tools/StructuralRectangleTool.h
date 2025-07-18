/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2022-11-27
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "Tool.h"

#include "../GenericEphemeralVisualizationRestorePayload.h"

#include <Game/GameAssetManager.h>

#include <Core/GameTypes.h>

#include <optional>

namespace ShipBuilder {

class StructuralRectangleTool final : public Tool
{
public:

    StructuralRectangleTool(
        Controller & controller,
        GameAssetManager const & gameAssetManager);

    ~StructuralRectangleTool();

    void OnMouseMove(DisplayLogicalCoordinates const & mouseCoordinates) override;
    void OnLeftMouseDown() override;
    void OnLeftMouseUp() override;
    void OnRightMouseDown() override {}
    void OnRightMouseUp() override {}
    void OnShiftKeyDown() override;
    void OnShiftKeyUp() override;
    void OnMouseLeft() override {}

protected:


private:

    ShipSpaceCoordinates GetCornerCoordinatesEngaged() const;

    ShipSpaceRect CalculateRect(ShipSpaceCoordinates const & cornerCoordinates) const;

    GenericEphemeralVisualizationRestorePayload DrawEphemeralRectangle(ShipSpaceRect const & rect);

    void UndoEphemeralRectangle();

    void UpdateEphViz();

    void DrawFinalRectangle(ShipSpaceRect const & rect);

    std::tuple<StructuralMaterial const *, std::optional<StructuralMaterial const *>> GetMaterials() const;

private:

    struct EngagementData
    {
        ShipSpaceCoordinates StartCorner;
        std::optional<GenericEphemeralVisualizationRestorePayload> EphVizRestorePayload;

        EngagementData(ShipSpaceCoordinates const & startCorner)
            : StartCorner(startCorner)
            , EphVizRestorePayload()
        {}
    };

    // When set, we're engaged (dragging)
    std::optional<EngagementData> mEngagementData;

    bool mIsShiftDown;
};

}
