# Floating Sandbox
Two-dimensional particle system in C++, simulating physical bodies floating in water and sinking.

# Overview
This game is a realistic two-dimensional particle system, which uses mass-spring networks to simulate rigid bodies (such as ships) floating in water and exchanging heat with their surroundings. You can punch holes in an object, slice it, apply forces and heat to it, set it on fire, and smash it with bomb explosions. Once water starts rushing inside the object, it will start sinking and, if you're lucky enough, it will start breaking up!

<img src="https://i.imgur.com/c8fTsgY.png">
<img src="https://i.imgur.com/kovxCty.png">
<img src="https://i.imgur.com/XHw3Jrl.png">

You can create your own physical objects by drawing images using colors that correspond to materials in the game's library. Each material has its own physical properties, such as mass, strength, stiffness, water permeability, specific heat, sound properties, and so on. The game comes with a handy template image that you can use to quickly select the right materials for your object.

If you want, you can also apply a higher-resolution image to be used as a more realistic texture for the object. Once you load your object, watch it float and explore how it behaves under stress!

For the physics in the simulation I'm trying to shy away from tricks that exist solely for the purpose of delivering an eye-candy; every bit is grounded as close as possible into real physics, and the material system has been put together using physical attributes of real-world materials. This makes it sometimes hard to build structures that sustain their own weight or float easily - as it is in reality, after all - but the reward is a realistic world-in-a-sandbox where every action and corresponding reaction are not pre-programmed but, rather, are generated automatically by the physics engine calculations.

The game currently comes with a few example objects - mostly ships - and I'm always busy making new ships and objects. Anyone is encouraged to make their own objects, and if you'd like them to be included in the game, just get in touch with me - you'll get proper recognition in the About dialog, of course.

The original idea for the game is from Luke Wren, who wrote a Sinking Ship Simulator to simulate sinking ships. I have adopted his idea, completely reimplemented his simulator, and revamped its feature set; at this moment it is really a generic physics simulator that can be used to simulate just about any 2D floating rigid body under stress.

There are lots of improvements that I'm currently working on; some of these are:
- In-game Ship Builder
- Improved rigid body simulation algorithm
- Hydrodynamics - to simulate parts gliding underwater
	- Requires maintaining convex hull and ship perimeter normals
- Waves and splashes originating from collisions with water
- Smoke from fire
- Multiple ships and collision detection among parts of the ships
- Ocean floor getting dents upon impact
- Morse code sound before sinking, and ship horns
- NPC's that move freely within ships

These and other ideas will come out with frequent releases.

The game is also featured at [GameJolt](https://gamejolt.com/games/floating-sandbox/353572), and plenty of videos may be found on Youtube.

# System Requirements
- Windows 7, 8, or 10, either 64-bit or 32-bit (Linux coming soon!)
	- The 64-bit build of Floating Sandbox runs ~7% faster than the 32-bit build, so if you're running a 64-bit Windows it is advisable to install the 64-bit build of Floating Sandbox
- OpenGL 2.1 or later
	- If your graphics card does not support OpenGL 2.1, try upgrading its drivers - most likely there's a newer version with support for 2.1

# Technical Details
This game is a C++ implementation of a particular class of particle systems, namely a *mass-spring-damper* network. With a mass-spring-damper network it is possible to simulate a rigid body by decomposing it into a number of infinitesimal particles ("points"), which are linked to each other via spring-damper pairs. Springs help maintain the rigidity of the body, while dampers are mostly to maintain the numerical stability of the system.

At any given moment, the forces acting on a point are:
- Spring forces, proportional to the elongation of the spring (Hooke's law) and thus to the positions of the two endpoints
- Damper forces, proportional to the relative velocity of the endpoints of the spring and thus to the velocity of the two endpoints
- Gravity and buoyance forces, proportional to the mass and "wetness" of the points
- Forces deriving from the interactions with the user, who can apply radial or angular forces, generate explosions, and so on

Water that enters the body moves following gravity and pressure gradients, and it adds to the mass of each "wet" point rendering parts of the body heavier.

Thermodynamics controls how heat is transferred among particles and their surroundings, and combustion controls what happens when particles reach their critical burning points.

Bodies are loaded from *png* images; each pixel in the image becomes a point in the simulated world, and springs connect each point to all of its neighbours. The color of the pixel in the original image determines the material of the corresponding point, based on a dictionary containing tens of materials; the material of a point in turn determines the physical properties of the point (e.g. mass, water permeability, electrical conductivity) and of the springs attached to it (e.g. stiffness, strength).

An optional texture map may be applied on top of the body, which will be drawn according to a tessellation of the network of points.

Users can interact with a body in different ways:
- Break and repair parts of the body
- Slice and smash the body in pieces
- Deploy and detonate bombs
- Pin individual points of the body so that their position (and velocity) becomes frozen
- Add or remove water to and from the body
- Add or remove heat to and from the body - and let it catch fire!
- Apply radial and angular forces
- Move or pull individual parts
- Generate waves

<img src="https://i.imgur.com/6LOVsqX.jpg">

# History
I started coding this game after stumbling upon Luke Wren's and Francis Racicot's (Pac0master) [Ship Sandbox](https://github.com/Wren6991/Ship-Sandbox). After becoming fascinated by it, I [forked](https://github.com/GabrieleGiuseppini/Ship-Sandbox) Luke's GitHub repo and started playing with the source code. After less than a year I had been making these major changes on the original codebase:
- Completely rewritten physics layer, making simulation more grounded in reality
- Completely rewritten data structures to maximize data locality
- Rewritten dynamics integration step to make full use of packed SSE floating point instructions on Intel x86
- Restructured interactions between the UI and the game, splitting settings between physics-related settings and render-related settings
- Rearchitected lifetime management of elements - originally elements were removed from vectors while these are being iterated, and the entire "points-to" graph was a tad too complex
- Completely rewritten the graphics layer, targeting OpenGL 2.0 "core profile" (i.e. no compatibility API) with custom shaders and texture mapping
- Added sounds and cued music
- Added lights that are powered by generators connected by cables, and flicker and turn off when the circuit is interrupted
- Added bombs
- Added connected component detection, used to correctly draw ship break-away parts on top of each other, among other things
- Upgraded to C++17
- Added ephemeral particles for debris when parts break, sparkles when metal is sliced, and bubbles when water enters the ship
- Implemented shallow water equations for interactive waves

...all of this while improving the game's FPS rate from 7 to 30 (on my 2009 laptop!)

After a while I realized that I had rewritten all of the original code, and that my new project was thus worthy of a new name and a new source code repository, the one you are looking at now.

So far I've also added the following:
- Thermodynamics, with flames and explosions
- Complex electrical elements (switches, pumps, flood doors, engines, etc.) and an electrical panel to control them all
- Storms with rain and lightnings
- Ability to persist and reload the ~100 simulation settings

# Performance Characteristics
The bottleneck at the moment is the spring relaxation algorithm, which requires about 80% of the time spent for the simulation of each single frame. I have an alternative version of the same algorithm written with intrinsics in the Benchmarks project, which shows a 20%-27% perf improvement. Sooner or later I'll integrate that in the game, but it's not gonna be a...game changer (pun intended). Instead, I plan to revisit the spring relaxation algorithm altogether after the next two major versions (see roadmap at https://gamejolt.com/games/floating-sandbox/353572/devlog/the-future-of-floating-sandbox-cdk2c9yi). There is a different family of algorithms based on minimization of potential energy, which supposedly requires less iterations and on top of that is easily parallelizable - the current iterative algorithm is not (easily) parallelize-able.
This said, in the current implementation, what matters the most is CPU speed - the whole simulation is basically single-threaded (some small steps are parallel, but they're puny compared with the spring relaxation). My laptop is a single-core, 2.2GHz Intel box, and the plain Titanic runs at ~22 FPS. The same ship on a 4-core, 1GHz Intel laptop runs at ~9FPS.

Rendering is a different story. At the time of writing, I'm moving all the rendering code to a separate thread, allowing simulation updates and rendering to run in parallel. Obviously, only multi-core boxes will benefit from parallel rendering, and boxes with very slow or emulated graphics hardware will benefit the most. In any case, at this moment rendering requires a fraction of the time needed for updating the simulation, so CPU speed still dominates the performance you get, compared to GPU speed. The 4-core box I mentioned earlier got a ~10% perf improvement, while another 2-core box - with a faster clock - showed a ~25% perf improvement.

# Building the Game
I build this game with Visual Studio 2019 (thus with full C++ 17 support).
I tried to do my best to craft the CMake files in a platform-independent way, but I'm working on this exclusively in Visual Studio, hence I'm sure some unportable features have slipped in. Feel free to send pull requests for CMake edits for other platforms.

In order to build the game, you will need the following dependencies:
- <a href="https://www.wxwidgets.org/">WxWidgets</a> v3.1.4 (cross-platform GUI library)*
- <a href="http://openil.sourceforge.net/">DevIL</a> (cross-platform image manipulation library)*
- <a href="https://www.sfml-dev.org/index.php">SFML</a> (cross-platform multimedia library)*
- <a href="https://github.com/kazuho/picojson">picojson</a> (header-only JSON parser and serializer)
- <a href="https://github.com/google/benchmark">Google Benchmark</a>
- <a href="https://github.com/google/googletest/">Google Test</a> release-1.10.0

Dependencies marked with * may be statically linked by using the `MSVC_USE_STATIC_LINKING` option.

A custom `UserSettings.cmake` may be used in order to configure the locations of all dependencies. If you want to use it, copy the `UserSettings.example.cmake` to `UserSettings.cmake` and adapt it to your setup. In case you do not want to use this file, you can use the example to get an overview of all CMake variables you might need to use to configure the dependencies.

Over the years we've been writing down OS-specific build steps:
- [Ubuntu](https://github.com/GabrieleGiuseppini/Floating-Sandbox/Building FS on Ubuntu.md)

# Contributing
At this moment I'm looking for volunteers for three specific tasks: creating a "Ship Editor" for the game, creating new ships, and building the game on non-Windows platforms.

With the "Ship Editor" a user would be able to craft a ship from nothing, picking materials out of a dictionary, laying out ropes, and adjusting texture maps to the ship's structure. The UI would also provide feedback on the current buoyancy of the ship and its mass center. Contact me if you'd like to apply! You need to know C++ as most of the physics code you'll use in the UI will come straight from the game's core library.

If you're more on the graphics side, instead, I'd like to collect your ships - and whatever other bodies you can imagine floating and sinking in water! Just send your ships to me and you'll get a proper *thank you* in the About dialog!

Finally, I'm looking for builders for non-Windows platforms. I'll also gladly accept any code contributions that may be necessary to ensure the project builds on multiple platforms.