/***************************************************************************************
 * Original Author:     Gabriele Giuseppini
 * Created:             2019-03-20
 * Copyright:           Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
 ***************************************************************************************/
#include "ShipDescriptionDialog.h"

#include <wx/button.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/wxhtml.h>

#include <sstream>

ShipDescriptionDialog::ShipDescriptionDialog(
    wxWindow* parent,
    ShipMetadata const & shipMetadata,
    bool isAutomatic,
    std::shared_ptr<UIPreferencesManager> uiPreferencesManager)
    : wxDialog(
        parent,
        wxID_ANY,
        shipMetadata.ShipName,
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP)
    , mUIPreferencesManager(std::move(uiPreferencesManager))
{
    SetBackgroundColour(wxColour("WHITE"));

    wxBoxSizer * topSizer = new wxBoxSizer(wxVERTICAL);

    {
        wxHtmlWindow *html = new wxHtmlWindow(
            this,
            wxID_ANY,
            wxDefaultPosition,
            wxSize(480, 240),
            wxHW_SCROLLBAR_AUTO);

        html->SetBorders(0);
        html->SetPage(MakeHtml(shipMetadata));
        html->SetSize(
            html->GetInternalRepresentation()->GetWidth(),
            html->GetInternalRepresentation()->GetHeight());

        topSizer->Add(html, 1, wxALL, 10);
    }

#if wxUSE_STATLINE
    topSizer->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
#endif // wxUSE_STATLINE

    {
        if (isAutomatic)
        {
            wxCheckBox * dontChk = new wxCheckBox(this, wxID_ANY, _("Don't show descriptions when ships are loaded"));
            dontChk->SetToolTip(_("Prevents ship descriptions from being shown each time a ship is loaded. You can always change this setting later from the \"Preferences\" window."));
            dontChk->SetValue(false);
            dontChk->Bind(wxEVT_CHECKBOX, &ShipDescriptionDialog::OnDontShowOnShipLoadheckboxChanged, this);

            topSizer->Add(dontChk, 0, wxALL | wxALIGN_LEFT, 10);
        }

        {
            wxButton * okButton = new wxButton(this, wxID_OK, _("OK"));
            okButton->SetDefault();

            topSizer->Add(okButton, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 10);
        }
    }

    this->SetSizerAndFit(topSizer);

    Centre(wxCENTER_ON_SCREEN | wxBOTH);
}

ShipDescriptionDialog::~ShipDescriptionDialog()
{
}

void ShipDescriptionDialog::OnDontShowOnShipLoadheckboxChanged(wxCommandEvent & event)
{
    mUIPreferencesManager->SetShowShipDescriptionsAtShipLoad(!event.IsChecked());
}

std::string ShipDescriptionDialog::MakeHtml(ShipMetadata const & shipMetadata)
{
    std::stringstream ss;

    ss << "<html> <body>";


    // Title

    ss << "<p align=center><font size=+2>";
    ss << shipMetadata.ShipName;
    ss << "</font></p>";


    // Metadata

    ////ss << "<font size=-1>";
    ////ss << "<table width=100%><tr>";
    ////ss << "<td align=left>";
    ////if (!!shipMetadata.YearBuilt)
    ////    ss << "<i>" << *(shipMetadata.YearBuilt) << "</i>";
    ////ss << "</td>";
    ////ss << "<td align=right>";
    ////if (!!shipMetadata.Author)
    ////    ss << "<i>Created by " << *(shipMetadata.Author) << "</i>";
    ////ss << "</td>";
    ////ss << "</tr></table>";
    ////ss << "</font>";



    // Description

    ss << "<p align=center>";

    if (!!shipMetadata.Description)
        ss << *(shipMetadata.Description);
    else
        ss << "This ship does not have a description.";

    ss << "</p>";



    ss << "</body> </html>";

    return ss.str();
}