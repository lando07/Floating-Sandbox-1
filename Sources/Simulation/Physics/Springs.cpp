/***************************************************************************************
 * Original Author:      Gabriele Giuseppini
 * Created:              2018-05-12
 * Copyright:            Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
 ***************************************************************************************/
#include "Physics.h"

#include <Core/Algorithms.h>

#include <cmath>

namespace Physics {

void Springs::Add(
    ElementIndex pointAIndex,
    ElementIndex pointBIndex,
    Octant factoryPointAOctant,
    Octant factoryPointBOctant,
    SuperTrianglesVector const & superTriangles,
    ElementCount coveringTrianglesCount,
    Points const & points)
{
    ElementIndex const springIndex = static_cast<ElementIndex>(mIsDeletedBuffer.GetCurrentPopulatedSize());

    mIsDeletedBuffer.emplace_back(false);

    mEndpointsBuffer.emplace_back(pointAIndex, pointBIndex);

    mFactoryEndpointOctantsBuffer.emplace_back(factoryPointAOctant, factoryPointBOctant);

    mSuperTrianglesBuffer.emplace_back(superTriangles);
    mFactorySuperTrianglesBuffer.emplace_back(superTriangles);

    assert(coveringTrianglesCount >= superTriangles.size()); // Covering triangles count includes super triangles
    mCoveringTrianglesCountBuffer.emplace_back(coveringTrianglesCount);

    // Strain threshold is average, and randomized - +/-
    float constexpr RandomWidth = 0.7f; // 70%: 35% less or 35% more
    float const averageStrainThreshold = (points.GetStructuralMaterial(pointAIndex).StrainThresholdFraction + points.GetStructuralMaterial(pointBIndex).StrainThresholdFraction) / 2.0f;
    float const strainThreshold = averageStrainThreshold
        * (1.0f - RandomWidth / 2.0f + RandomWidth * points.GetRandomNormalizedUniformPersonalitySeed(pointAIndex));

    mStrainStateBuffer.emplace_back(
        0.0f, // Breaking elongation recalculated later
        strainThreshold,
        false);

    mFactoryRestLengthBuffer.emplace_back((points.GetPosition(pointAIndex) - points.GetPosition(pointBIndex)).length());
    mRestLengthBuffer.emplace_back((points.GetPosition(pointAIndex) - points.GetPosition(pointBIndex)).length());

    // Dynamics coefficients recalculated later, but stiffness grows slowly and shrinks fast, hence we want to start high
    mStiffnessCoefficientBuffer.emplace_back(std::numeric_limits<float>::max());
    mDampingCoefficientBuffer.emplace_back(0.0f);

    // Stiffness is average
    float const averageStiffness =
        (points.GetStructuralMaterial(pointAIndex).Stiffness + points.GetStructuralMaterial(pointBIndex).Stiffness)
        / 2.0f;

    // Strength is average
    float const averageStrength =
        (points.GetStrength(pointAIndex) + points.GetStrength(pointBIndex))
        / 2.0f;

    // Melting temperature is average
    float const averageMeltingTemperature =
        (points.GetStructuralMaterial(pointAIndex).MeltingTemperature + points.GetStructuralMaterial(pointBIndex).MeltingTemperature)
        / 2.0f;

    mMaterialPropertiesBuffer.emplace_back(
        averageStiffness,
        averageStrength,
        averageMeltingTemperature,
        CalculateExtraMeltingInducedTolerance(averageStrength));

    // Base structural material is arbitrarily the weakest of the two;
    // only affects sound and name, anyway
    mBaseStructuralMaterialBuffer.emplace_back(
        points.GetStructuralMaterial(pointAIndex).Strength < points.GetStructuralMaterial(pointBIndex).Strength
        ? &(points.GetStructuralMaterial(pointAIndex))
        : &(points.GetStructuralMaterial(pointBIndex)));

    // If both nodes are rope, then the spring is rope
    // (non-rope <-> rope springs are "connections" and not to be treated as ropes)
    mIsRopeBuffer.emplace_back(points.IsRope(pointAIndex) && points.IsRope(pointBIndex));

    // Spring is permeable by default - will be changed later
    mWaterPermeabilityBuffer.emplace_back(1.0f);

    // Heat properties are average
    float const thermalConductivity =
        (points.GetStructuralMaterial(pointAIndex).ThermalConductivity + points.GetStructuralMaterial(pointBIndex).ThermalConductivity)
        / 2.0f;
    mMaterialThermalConductivityBuffer.emplace_back(thermalConductivity);

    // Make room
    mCachedVectorialLengthBuffer.emplace_back(0.0f);
    mCachedVectorialNormalizedVectorBuffer.emplace_back(vec2f::zero());

    // Calculate parameters for this spring
    UpdateCoefficients(
        springIndex,
        points);
}

void Springs::Destroy(
    ElementIndex springElementIndex,
    DestroyOptions destroyOptions,
    float currentSimulationTime,
    SimulationParameters const & simulationParameters,
    Points const & points)
{
    assert(!IsDeleted(springElementIndex));

    // Invoke destroy handler
    assert(nullptr != mShipPhysicsHandler);
    mShipPhysicsHandler->HandleSpringDestroy(
        springElementIndex,
        !!(destroyOptions & Springs::DestroyOptions::DestroyAllTriangles),
        currentSimulationTime,
        simulationParameters);

    // Fire spring break event, unless told otherwise
    if (!!(destroyOptions & Springs::DestroyOptions::FireBreakEvent))
    {
        mSimulationEventHandler.OnBreak(
            GetBaseStructuralMaterial(springElementIndex),
            mParentWorld.GetOceanSurface().IsUnderwater(GetEndpointAPosition(springElementIndex, points)), // Arbitrary
            1);
    }

    // Zero out our dynamics coefficients, so that we can still calculate Hooke's
    // and damping forces for this spring without running the risk of
    // affecting non-deleted points
    mStiffnessCoefficientBuffer[springElementIndex] = 0.0f;
    mDampingCoefficientBuffer[springElementIndex] = 0.0f;

    // Flag ourselves as deleted
    mIsDeletedBuffer[springElementIndex] = true;
}

void Springs::Restore(
    ElementIndex springElementIndex,
    SimulationParameters const & simulationParameters,
    Points const & points)
{
    assert(IsDeleted(springElementIndex));

    // Clear the deleted flag
    mIsDeletedBuffer[springElementIndex] = false;

    // Recalculate coefficients for this spring
    UpdateCoefficients(
        springElementIndex,
        points);

    // Invoke restore handler
    assert(nullptr != mShipPhysicsHandler);
    mShipPhysicsHandler->HandleSpringRestore(
        springElementIndex,
        simulationParameters);
}

void Springs::UpdateForSimulationParameters(
    SimulationParameters const & simulationParameters,
    Points const & points)
{
    if (simulationParameters.NumMechanicalDynamicsIterations<float>() != mCurrentNumMechanicalDynamicsIterations
        || simulationParameters.SpringStiffnessAdjustment != mCurrentSpringStiffnessAdjustment
        || simulationParameters.SpringDampingAdjustment != mCurrentSpringDampingAdjustment
        || simulationParameters.SpringStrengthAdjustment != mCurrentSpringStrengthAdjustment
        || simulationParameters.MeltingTemperatureAdjustment != mCurrentMeltingTemperatureAdjustment)
    {
        // Update our version of the parameters
        mCurrentNumMechanicalDynamicsIterations = simulationParameters.NumMechanicalDynamicsIterations<float>();
        mCurrentStrengthIterationsAdjustment = CalculateSpringStrengthIterationsAdjustment(mCurrentNumMechanicalDynamicsIterations);
        mCurrentSpringStiffnessAdjustment = simulationParameters.SpringStiffnessAdjustment;
        mCurrentSpringDampingAdjustment = simulationParameters.SpringDampingAdjustment;
        mCurrentSpringStrengthAdjustment = simulationParameters.SpringStrengthAdjustment;
        mCurrentMeltingTemperatureAdjustment = simulationParameters.MeltingTemperatureAdjustment;

        // Recalc whole
        UpdateCoefficientsForPartition(
            0, 1,
            points);
    }
}

void Springs::UploadElements(
    ShipId shipId,
    RenderContext & renderContext) const
{
    // Either upload all springs, or just the edge springs
    bool const doUploadAllSprings =
        DebugShipRenderModeType::Springs == renderContext.GetDebugShipRenderMode();

    // Ropes are uploaded as springs only if DebugRenderMode is springs or edge springs
    bool const doUploadRopesAsSprings =
        DebugShipRenderModeType::Springs == renderContext.GetDebugShipRenderMode()
        || DebugShipRenderModeType::EdgeSprings == renderContext.GetDebugShipRenderMode();

    auto & shipRenderContext = renderContext.GetShipRenderContext(shipId);

    for (ElementIndex i : *this)
    {
        // Only upload non-deleted springs that are not covered by two super-triangles, unless
        // we are in springs render mode
        if (!mIsDeletedBuffer[i])
        {
            if (IsRope(i) && !doUploadRopesAsSprings)
            {
                shipRenderContext.UploadElementRope(
                    GetEndpointAIndex(i),
                    GetEndpointBIndex(i));
            }
            else if (
                mCoveringTrianglesCountBuffer[i] < 2
                || doUploadAllSprings
                || IsRope(i))
            {
                shipRenderContext.UploadElementSpring(
                    GetEndpointAIndex(i),
                    GetEndpointBIndex(i));
            }
        }
    }
}

void Springs::UploadStressedSpringElements(
    ShipId shipId,
    RenderContext & renderContext) const
{
    auto & shipRenderContext = renderContext.GetShipRenderContext(shipId);

    for (ElementIndex i : *this)
    {
        if (!mIsDeletedBuffer[i])
        {
            if (mStrainStateBuffer[i].IsStressed)
            {
                shipRenderContext.UploadElementStressedSpring(
                    GetEndpointAIndex(i),
                    GetEndpointBIndex(i));
            }
        }
    }
}

void Springs::UpdateForStrainsAndCacheSpringVectors(
    float currentSimulationTime,
    SimulationParameters const & simulationParameters,
    Points & points,
    StressRenderModeType stressRenderMode)
{
    if (stressRenderMode == StressRenderModeType::None)
    {
        InternalUpdateForStrainsAndCacheSpringVectors<false>(currentSimulationTime, simulationParameters, points);
    }
    else
    {
        InternalUpdateForStrainsAndCacheSpringVectors<true>(currentSimulationTime, simulationParameters, points);
    }
}

////////////////////////////////////////////////////////////////////

template<bool DoUpdateStress>
void Springs::InternalUpdateForStrainsAndCacheSpringVectors(
    float currentSimulationTime,
    SimulationParameters const & simulationParameters,
    Points & points)
{
    float constexpr StrainLowWatermark = 0.08f; // Less than this multiplier to become non-stressed

    OceanSurface const & oceanSurface = mParentWorld.GetOceanSurface();
    vec2f const * restrict const positionBuffer = points.GetPositionBufferAsVec2();
    Endpoints const * restrict const endpointsBuffer = GetEndpointsBuffer();
    float * restrict const cachedLengthBuffer = mCachedVectorialLengthBuffer.data();
    vec2f * restrict const cachedNormalizedVectorBuffer = mCachedVectorialNormalizedVectorBuffer.data();

    // Visit all springs
    assert(is_aligned_to_float_element_count(GetBufferElementCount()));
    for (ElementIndex s_0 = 0; s_0 < GetBufferElementCount(); s_0 += 4)
    {
        //
        // Calculate and cache vector info for these four springs
        //

        Algorithms::CalculateSpringVectors(
            s_0,
            positionBuffer,
            endpointsBuffer,
            cachedLengthBuffer,
            cachedNormalizedVectorBuffer);

        //
        // Do strain checks on these four springs now
        //

        for (ElementIndex s = s_0, i = 0; i < 4; ++i, ++s)
        {
            // Avoid breaking deleted springs
            if (!mIsDeletedBuffer[s])
            {
                auto & strainState = mStrainStateBuffer[s];

                // Calculate strain
                float const strain = cachedLengthBuffer[s] - mRestLengthBuffer[s];
                float const absStrain = std::abs(strain);

                // Check against breaking elongation
                float const breakingElongation = strainState.BreakingElongation;
                if (absStrain > breakingElongation)
                {
                    // It's broken!

                    // Destroy this spring
                    this->Destroy(
                        s,
                        DestroyOptions::FireBreakEvent // Notify Break
                        | DestroyOptions::DestroyAllTriangles,
                        currentSimulationTime,
                        simulationParameters,
                        points);
                }
                else
                {
                    if (strainState.IsStressed)
                    {
                        // Stressed spring...
                        // ...see if should un-stress it

                        if (absStrain < StrainLowWatermark * breakingElongation)
                        {
                            // It's not stressed anymore
                            strainState.IsStressed = false;
                        }
                    }
                    else
                    {
                        // Not stressed spring
                        // ...see if should stress it

                        if (absStrain > strainState.StrainThresholdFraction * breakingElongation)
                        {
                            // It's stressed!
                            strainState.IsStressed = true;

                            // Notify stress
                            mSimulationEventHandler.OnStress(
                                GetBaseStructuralMaterial(s),
                                oceanSurface.IsUnderwater(GetEndpointAPosition(s, points)), // Arbitrary
                                1);
                        }
                    }

                    // Update stress
                    if constexpr (DoUpdateStress)
                    {
                        float const stress = strain / breakingElongation; // Between -1.0 and +1.0

                        if (std::abs(stress) > std::abs(points.GetStress(GetEndpointAIndex(s))))
                        {
                            points.SetStress(
                                GetEndpointAIndex(s),
                                stress);
                        }

                        if (std::abs(stress) > std::abs(points.GetStress(GetEndpointBIndex(s))))
                        {
                            points.SetStress(
                                GetEndpointBIndex(s),
                                stress);
                        }
                    }
                }
            }
        }
    }
}

void Springs::UpdateCoefficientsForPartition(
    ElementIndex partition,
    ElementIndex partitionCount,
    Points const & points)
{
    ElementCount const partitionSize = (GetElementCount() / partitionCount) + ((GetElementCount() % partitionCount) ? 1 : 0);
    ElementCount const startSpringIndex = partition * partitionSize;
    ElementCount const endSpringIndex = std::min(startSpringIndex + partitionSize, GetElementCount());
    for (ElementIndex s = startSpringIndex; s < endSpringIndex; ++s)
    {
        if (!IsDeleted(s))
        {
            inline_UpdateCoefficients(
                s,
                points);
        }
    }
}

void Springs::UpdateCoefficients(
    ElementIndex springIndex,
    Points const & points)
{
    inline_UpdateCoefficients(
        springIndex,
        points);
}

void Springs::inline_UpdateCoefficients(
    ElementIndex springIndex,
    Points const & points)
{
    auto const endpointAIndex = GetEndpointAIndex(springIndex);
    auto const endpointBIndex = GetEndpointBIndex(springIndex);

    float const massFactor =
        (points.GetAugmentedMaterialMass(endpointAIndex) * points.GetAugmentedMaterialMass(endpointBIndex))
        / (points.GetAugmentedMaterialMass(endpointAIndex) + points.GetAugmentedMaterialMass(endpointBIndex));

    float const dt = SimulationParameters::SimulationStepTimeDuration<float> / mCurrentNumMechanicalDynamicsIterations;

    // Note: in 1.14 the spring temperature was the average of the two points.
    // Differences in temperature between adjacent points made it so that springs'
    // melting was widely underestimated.
    // In reality, a spring is as "soft" as its softest point.
    float const springTemperature = std::max(
        points.GetTemperature(endpointAIndex),
        points.GetTemperature(endpointBIndex));

    // Excedence of temperature over melting temperature; might be negative
    // if we're below the melting temperature
    float const meltingOverheat =
        springTemperature
        - GetMaterialMeltingTemperature(springIndex) * mCurrentMeltingTemperatureAdjustment;

    //
    // Stiffness coefficient
    //
    // The "stiffness coefficient" is the factor which, once multiplied with the spring displacement,
    // yields the spring force, according to Hooke's law.
    //
    // We calculate the coefficient so that the two forces applied to each of the two masses produce a resulting
    // change in position equal to a fraction SpringReductionFraction * adjustment of the spring displacement,
    // in the time interval of a single mechanical dynamics simulation.
    //
    // After one iteration the spring displacement dL = L - L0 is reduced to:
    //  dL * (1-SRF)
    // where SRF is the (adjusted) SpringReductionFraction parameter. After N iterations this would be:
    //  dL * (1-SRF)^N
    //
    // The reduction adjustment is both the material-specific adjustment and the global game adjustment.
    //
    // If the endpoints are melting, their temperature also controls the stiffness - the higher the temperature,
    // above the melting point, the lower the stiffness; this is achieved with a smoothed multiplier with the following
    // edges:
    //  T <= Tm                    :    1.0
    //  T >= Tm + DeltaMeltingTMax :    ~< 1.0 (== MinStiffnessFraction, asymptote)
    //

    // Asymptote
    // NOTE: This value should be adjusted based on the number of spring iterations we perform
    // per simulation step
    float constexpr MinStiffnessFraction = 0.0002f;

    // We reach max softness at T+200
    float const meltDepthFraction = SmoothStep(0.0f, 200.0f, meltingOverheat);

    // 1.0 when not melting, MinStiffnessFraction when melting "a lot"
    float const meltMultiplier = Mix(1.0f, MinStiffnessFraction, meltDepthFraction);

    // Our desired stiffness coefficient
    float const desiredStiffnessCoefficient =
        SimulationParameters::SpringReductionFraction
        * GetMaterialStiffness(springIndex)
        * mCurrentSpringStiffnessAdjustment
        * massFactor
        / (dt * dt)
        * meltMultiplier;

    // If the coefficient is growing (spring is becoming more stiff), then
    // approach the desired stiffness coefficient slowly,
    // or else we have too much discontinuity and might explode.
    // Note: this is wanted for cooling a melted spring, but it also gets
    // in the way when we increase the number of iterations, as the ship takes
    // a while to reach the target stiffness.
    if (desiredStiffnessCoefficient > mStiffnessCoefficientBuffer[springIndex])
    {
        mStiffnessCoefficientBuffer[springIndex] +=
            0.03f // 0.03: ~76 steps to 1/10th off target
            * (desiredStiffnessCoefficient - mStiffnessCoefficientBuffer[springIndex]);
    }
    else
    {
        // Sudden decrease
        mStiffnessCoefficientBuffer[springIndex] = desiredStiffnessCoefficient;
    }

    //
    // Damping coefficient
    //
    // Magnitude of the drag force on the relative velocity component along the spring.
    //

    mDampingCoefficientBuffer[springIndex] =
        SimulationParameters::SpringDampingCoefficient
        * mCurrentSpringDampingAdjustment
        * massFactor
        / dt;

    //
    // Breaking elongation
    //
    // The breaking elongation - i.e. the max delta L, aka displacement tolerance - depends on:
    //  - The material strength and the strength adjustment
    //  - The spring's decay (which itself is a function of the endpoints' decay)
    //  - If the endpoints are melting, their temperature - so to keep springs intact while melting makes them longer
    //  - The actual number of mechanics iterations we'll be performing
    //
    // The breaking elongation is the strength multiplied with the spring's rest length, so that it's ready to be
    // compared against the spring's absolute delta L without having to divide the delta L by the rest length
    //

    // Decay of spring == avg of two endpoints' decay
    float const springDecay =
        (points.GetDecay(endpointAIndex) + points.GetDecay(endpointBIndex))
        / 2.0f;

    // If we're melting, the current spring length, when longer than the
    // previous rest length, is also its new rest length - but no more than a few times
    // the factory rest length, or else springs become abnormally-long spikes.
    // When cooling again, we leave the rest length at its maximum - modeling permanent deformation.
    if (meltingOverheat > 0.0f)
    {
        SetRestLength(
            springIndex,
            Clamp(
                GetLength(springIndex, points),
                GetRestLength(springIndex),
                mFactoryRestLengthBuffer[springIndex] * 2.0f));
    }

    mStrainStateBuffer[springIndex].BreakingElongation =
        GetMaterialStrength(springIndex)
        * mCurrentSpringStrengthAdjustment
        * mCurrentStrengthIterationsAdjustment
        * springDecay
        * GetRestLength(springIndex) // To make strain comparison independent from rest length
        * (1.0f + GetExtraMeltingInducedTolerance(springIndex) * meltDepthFraction); // When melting, springs are more tolerant to elongation
}

float Springs::CalculateSpringStrengthIterationsAdjustment(float numMechanicalDynamicsIterations)
{
    // We need to adjust the strength - i.e. the displacement tolerance or spring breaking point - based
    // on the actual number of mechanics iterations we'll be performing.
    //
    // After one iteration the spring displacement dL = L - L0 is reduced to:
    //  dL * (1-SRF)
    // where SRF is the value of the SpringReductionFraction parameter. After N iterations this would be:
    //  dL * (1-SRF)^N
    //
    // This formula suggests a simple exponential relationship, but empirical data (e.g. auto-stress on the Titanic)
    // suggest the following relationship:
    //
    //  y = 0.2832163 + 9.209594*e^(-0.1142279*x)
    //
    // Where x is the total number of iterations.

    float const adjustment = 0.2832163f + 9.209594f * std::exp(-0.1142279f * numMechanicalDynamicsIterations);

    return adjustment;
}

float Springs::CalculateExtraMeltingInducedTolerance(float strength)
{
    // The extra elongation tolerance due to melting:
    //  - For small factory tolerances (~0.1), we are keen to get up to many times that tolerance
    //  - For large factory tolerances (~5.0), we are keen to get up to fewer times that tolerance
    //    (i.e. allow smaller change in length)
    float constexpr MaxMeltingInducedTolerance = 20;
    float constexpr MinMeltingInducedTolerance = 0.0f;
    float constexpr StartStrength = 0.3f; // At this strength, we allow max tolerance
    float constexpr EndStrength = 3.0f; // At this strength, we allow min tolerance

    return MaxMeltingInducedTolerance -
        (MaxMeltingInducedTolerance - MinMeltingInducedTolerance)
        / (EndStrength - StartStrength)
        * (Clamp(strength, StartStrength, EndStrength) - StartStrength);
}

}