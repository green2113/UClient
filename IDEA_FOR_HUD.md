# HUD Idea: Frosted Finish Prediction Block

The previous procedural `DrawBlurRect` look is useful as a HUD style idea, but it is not a real backdrop blur.

## Visual Direction

- Dark translucent rounded HUD block.
- Soft outside shadow/feather around the block.
- Light frosted overlay inside the block, enough to separate text and progress from the map.
- Keep the tee marker and progress bar crisp on top.

## Important Note

The old shader-only mist/falloff version should be treated as a design reference only. The production version should use a real backdrop blur, for example Kawase blur, by sampling already rendered framebuffer content behind the rect.
