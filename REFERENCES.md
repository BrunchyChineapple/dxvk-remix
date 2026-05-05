# References — Fork Atmosphere and Sky System

Academic and industry references for the physically-based sky, atmosphere, and
cloud rendering system added on the `MW-sky-Main` / `test-nvidia-sync` fork.
This file covers only the fork's additions; see `LICENSE` and upstream
`README.md` for Nvidia dxvk-remix attribution.

---

## Primary Papers

### Hillaire 2016 — Atmosphere and multiple-scattering framework

> Hillaire, Sébastien. *"Physically Based Sky, Atmosphere and Cloud Rendering
> in Frostbite."* In *Physically Based Shading in Theory and Practice*,
> SIGGRAPH 2016 Courses, ACM.
> [Frostbite news post](https://www.ea.com/frostbite/news/physically-based-sky-atmosphere-and-cloud-rendering)
> · [ACM DL](https://dl.acm.org/doi/10.1145/2897826.2927353)
> · [Author's page](https://sebh.github.io/publications/)

Used for:
- The whole Hillaire atmospheric scattering pipeline (transmittance LUT,
  multiscattering LUT, sky-view LUT).
- Section 3.4: ozone absorption and its contribution to sunset colors.
- Section 5.6.3, Equation 17: energy-conserving analytical integration for
  in-scattering along a ray.
- The high-level cloud rendering approach (phase function + single-scatter
  approximation for lighting) that inspired our simplified inline cloud
  implementation.

### Jensen, Durand, Stark, Premožze, Dorsey, Shirley 2001 — Night sky model

> Jensen, Henrik Wann; Durand, Frédo; Stark, Michael M.; Premožze, Simon;
> Dorsey, Julie; Shirley, Peter. *"A Physically-Based Night Sky Model."*
> In *Computer Graphics (SIGGRAPH 2001 Proceedings)*, ACM.
> [Stanford project page](https://graphics.stanford.edu/papers/nightsky/)
> · [ACM DL](https://dl.acm.org/doi/10.1145/383259.383306)

Used for:
- Overall approach to composing a night sky out of star field, Moon
  illumination, airglow, and atmospheric scattering.
- The conceptual framework for our procedural moon shading (Secunda and
  Masser disks, phase terminator, surface albedo variation).
- Airglow / ambient night sky brightness as a separate additive term.

Note: our implementation is purely procedural (no measured star catalogue,
no photographic Milky Way texture, no spectral BRDF). The Jensen et al.
paper is the reference for the composition of components and for the
underlying physics of each one.

---

## Standard Formulas and Algorithms

### Henyey–Greenstein phase function

> Henyey, L.G.; Greenstein, J.L. *"Diffuse radiation in the galaxy."*
> *Astrophysical Journal*, vol. 93, pp. 70–83, 1941.
> [DOI 10.1086/144246](https://doi.org/10.1086/144246)
> · [ADS](https://ui.adsabs.harvard.edu/abs/1941ApJ....93...70H/abstract)

Used for all anisotropic scattering in the fork: Mie aerosol scattering,
cloud forward-scatter (silver lining), and volumetric sun sampling.

### Kasten–Young air mass formula

> Kasten, F.; Young, A.T. *"Revised optical air mass tables and approximation
> formula."* *Applied Optics*, vol. 28, no. 22, pp. 4735–4738, 1989.
> [DOI 10.1364/AO.28.004735](https://doi.org/10.1364/AO.28.004735)

Used in the analytical sun-transmittance path in `evalAtmosphereRadiance`
when bypassing the transmittance LUT. The comment in the shader already
cites the formula by name; this entry provides the full reference.

### Beer–Lambert extinction

Used throughout for exponential attenuation of light through the
atmosphere, through the cloud layer, and for cloud self-shadowing.
The law is public-domain 18th/19th-century physics (Bouguer 1729 /
Lambert 1760 / Beer 1852) and does not require a modern citation, but
is noted here for completeness.

---

## Code Sources

### Blender — multiple-scattering approximation

Files that draw from Blender's sky shader implementation:
- `shaders/rtx/pass/atmosphere/multiscattering_lut.comp.slang`
- `shaders/rtx/pass/atmosphere/atmosphere_common.slangh`
  (`computeGroundReflectionAnalytical`, `computeAnalyticalMultiscattering`)

Specifically referenced: Blender's `sky_multiple_scattering.cpp` and the
ground-albedo default of 0.3.

> Blender Foundation. *Blender.* Licensed under GPL v2+ / Apache 2.0
> (source available at [projects.blender.org](https://projects.blender.org/blender/blender)).

The shader comments note that Blender's fitted coefficients were adjusted
to avoid an excessive purple sunset cast in our rendering; the structure
and intent of the approximation follow Blender.

### Nvidia dxvk-remix (upstream)

Everything not modified on this fork remains under the NVIDIA dxvk-remix
license. See upstream `LICENSE` and `README.md` in the repository root.

### Remix Plus (base fork)

Our fork branches from the Remix Plus community fork of dxvk-remix; see
`RemixProjGroup/dxvk-remix` on GitHub.

---

## Cloud Layer (this session's addition)

The procedural cloud system (`evalClouds` and helpers in
`atmosphere_common.slangh`) was written from scratch but builds on ideas
from the Hillaire and Jensen et al. papers above, plus:

- **FBM noise / fractional Brownian motion** — a classical technique dating
  to Mandelbrot; used here via the pre-existing `fbmNoise2D` function in
  `atmosphere_common.slangh` (originally added for moon surface detail and
  the galactic band in the star field).
- **Single-scatter cloud lighting** — the "sample density along the sun
  ray, exponentiate for Beer–Lambert transmittance" pattern we use for
  cloud self-shadowing is a standard technique in real-time cloud
  rendering. The clearest industry references are:
  - Schneider, Andrew; Vos, Nathan. *"The Real-time Volumetric Cloudscapes
    of Horizon Zero Dawn."* SIGGRAPH 2015, Advances in Real-time Rendering
    course. ([Guerrilla Games / Decima](https://www.guerrilla-games.com/media/News/Files/The-Real-time-Volumetric-Cloudscapes-of-Horizon-Zero-Dawn.pdf))
  - Hillaire 2016 (above) — the cloud rendering portion of the same course.

Our implementation is much simpler than either — a single analytical
sphere-shell layer rather than a true volumetric march — but the single-
scatter + phase-function shading model is shared.

---

## How to add new references

When committing new sky/atmosphere code, add any new references to this
file in the same commit. For shader files, also add a short inline
reference block under the existing license header naming the papers used
in that file. Keep the full citations here; keep the inline notes short.
