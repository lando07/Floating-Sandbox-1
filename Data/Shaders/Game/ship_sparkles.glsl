###VERTEX-120

#define in attribute
#define out varying

// Inputs
in vec4 inSparkle1; // Position, PlaneId, Progress
in vec4 inSparkle2; // VelocityVector, SparkleSpacePosition

// Outputs
out float progress;
out vec2 velocityVector;
out vec2 sparkleSpacePosition;

// Params
uniform mat4 paramOrthoMatrix;

void main()
{
    progress = inSparkle1.w;
    velocityVector = inSparkle2.xy;
    sparkleSpacePosition = inSparkle2.zw;

    gl_Position = paramOrthoMatrix * vec4(inSparkle1.xyz, 1.0);
}

###FRAGMENT-120

#define in varying

// Inputs from previous shader
in float progress; // [0.0, 1.0]
in vec2 velocityVector; // [(-1.0, -1.0), (1.0, 1.0)]
in vec2 sparkleSpacePosition; // [(-1.0, -1.0), (1.0, 1.0)]

void main()
{
    // v = (0.0, 0.0)
    // w = -velocity
    // p = uw
	// Consider the line extending the segment, parameterized as v + t (w - v).
  	// We find projection of point p onto the line. 
  	// It falls where t = [(p-v) . (w-v)] / |w-v|^2
  	// We clamp t from [0,1] to handle points outside the segment vw.
    float l2 = (velocityVector.x * velocityVector.x + velocityVector.y * velocityVector.y);  // i.e. |w-v|^2 -  avoid a sqrt
    float t = dot(sparkleSpacePosition, velocityVector) / l2;
    t = clamp(t, -1.0, 1.0);
    vec2 projection = t * velocityVector;
    float vectorDistance = length(projection - sparkleSpacePosition);
    
    // Alpha: 1.0 at center, 0.0 at edge
    #define LineThickness 0.1
    float alpha = 1.0 - smoothstep(0.0, 1.0, vectorDistance/LineThickness);
    
    // More transparent for t between 0 and -1
    alpha *= smoothstep(-1.5, 1.0, t);

    // Leave early outside of sparkle
    if (alpha < 0.01)
        discard;
    
    // progress = 0.0: whole sparkle is yellow/white
    // progress > 0.0: sparkle is orange at t=-1 end, yellow/white at t=1 end
    vec3 col = mix(
        vec3(1.0, 1.0, 0.80),	// yellow/white
        vec3(0.8, 0.50, 0.14), 	// orange        
        smoothstep(0.1, 0.45, progress) * (1.0 - (t + 1.0) / 2.0));

    // Higher progress => more transparent sparkle
    alpha *= 1.0 - progress;

    gl_FragColor = vec4(col, alpha);
}
