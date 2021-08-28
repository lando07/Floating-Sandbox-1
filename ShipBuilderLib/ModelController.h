/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2021-08-28
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "IUserInterface.h"
#include "Model.h"
#include "View.h"

#include <memory>

namespace ShipBuilder {

/*
 * This class implements operations on the model.
 */
class ModelController
{
public:

    ModelController(
        IUserInterface & userInterface,
        View & view);

private:

    IUserInterface & mUserInterface;
    View & mView;

    // TODO: decide if member of uq_ptr
    std::unique_ptr<Model> mModel;
};

}