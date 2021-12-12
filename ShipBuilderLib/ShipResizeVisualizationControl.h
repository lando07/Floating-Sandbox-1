/***************************************************************************************
 * Original Author:     Gabriele Giuseppini
 * Created:             2021-12-09
 * Copyright:           Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
 ***************************************************************************************/
#pragma once

#include <GameCore/GameTypes.h>
#include <GameCore/ImageData.h>

#include <wx/dc.h>
#include <wx/image.h>
#include <wx/panel.h>

namespace ShipBuilder {

class ShipResizeVisualizationControl : public wxPanel
{
public:

    ShipResizeVisualizationControl(
        wxWindow * parent,
        int width,
        int height);

    IntegralCoordinates const & GetOffset() const
    {
        return mOffset;
    }

    void Initialize(
        RgbaImageData const & image,
        IntegralRectSize const & targetSize);

    void Deinitialize();
    
    void SetTargetSize(IntegralRectSize const & targetSize);
    void SetAnchor(int anchorMatrixX, int anchorMatrixY);

private:

    void OnPaint(wxPaintEvent & event);
    void OnChange();

    void Render(wxDC & dc);

private:

    wxPen mTargetPen;
    wxBrush mTargetBrush;

    wxImage mImage;
    IntegralRectSize mTargetSize;
    IntegralCoordinates mOffset;

    // Calculated members
    wxPoint mTargetOriginDC;
    wxSize mTargetSizeDC;
    wxBitmap mResizedBitmap;
    wxPoint mResizedBitmapOriginDC;
};

}