/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2019-10-15
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "GameEventDispatcher.h"
#include "GameParameters.h"
#include "RenderContext.h"

#include <GameCore/GameWallClock.h>

namespace Physics
{

class Storm
{

public:

	Storm(std::shared_ptr<GameEventDispatcher> gameEventDispatcher);

    void Update(GameParameters const & gameParameters);

    void Upload(Render::RenderContext & renderContext) const;

public:

    struct Parameters
    {
        float WindSpeed; // Km/h, absolute (on top of current direction)
        unsigned int NumberOfClouds;
        float CloudsSize; // [0.0f = initial size, 1.0 = full size]
        float CloudDarkening; // [0.0f = full darkness, 1.0 = no darkening]
        float AmbientDarkening; // [0.0f = full darkness, 1.0 = no darkening]
		float RainDensity; // [0.0f = no rain, 1.0f = full rain]

        Parameters()
        {
            Reset();
        }

        void Reset()
        {
            WindSpeed = 0.0f;
            NumberOfClouds = 0;
            CloudsSize = 0.0f;
            CloudDarkening = 1.0f;
            AmbientDarkening = 1.0f;
			RainDensity = 0.0f;
        }
    };

    Parameters const & GetParameters() const
    {
        return mParameters;
    }

    void TriggerStorm();

	void TriggerLightning();

private:

    void TurnStormOn(GameWallClock::time_point now);
    void TurnStormOff();

private:

	std::shared_ptr<GameEventDispatcher> mGameEventHandler;

    Parameters mParameters;

    //
    // State machine
    //

    // Flag indicating whether we are in a storm or waiting for one
    bool mIsInStorm;

    // The current progress of the storm, when in a storm: [0.0, 1.0]
    float mCurrentStormProgress;

	// The CDF for thunders
	float const mThunderCdf;

	// The next timestamp at which to sample the Poisson distribution for deciding thunders
	GameWallClock::time_point mNextThunderPoissonSampleTimestamp;

    // The timestamp at which we last did a storm update
    GameWallClock::time_point mLastStormUpdateTimestamp;
};

}
