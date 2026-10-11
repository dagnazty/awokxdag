# Website branding

- `public/brand/logo.jpg` is the user's original, unmodified logo. Header and footer show it on a square white tile using CSS object-fit.
- `brand-source/favicon-master.png` is the square favicon adaptation prepared with the built-in image tool. It is retained for future exports and is not shipped with the site.
- `public/brand/favicon-16.png`, `favicon-32.png`, and `favicon-48.png` are downscaled PNG exports made with macOS `sips`.
- `public/favicon.ico` contains those three PNG sizes in an ICO container.
- `public/brand/apple-touch-icon.png` is the 180 × 180 home-screen icon.

The build copies `public/` into the generated output. All image and icon URLs use the page's relative prefix, including technical notes nested under `docs/`.

## Favicon preparation prompt (built-in image tool)

Use case: precise-object-edit. Asset type: square website favicon derived from the user's existing logo. Edit target: the attached black-and-white skull / predator-like head logo and surrounding broken circular ring. Preserve the EXACT existing skull, long fangs, dark side locks and broken circular ring shape, proportions, positions, and monochrome linework. Do not redesign, reinterpret, embellish, add text, or change the logo. Only remove excess outer blank margins and fit the full existing emblem into a square canvas with a flat clean white background and a small even margin (about 4%) on each edge. Keep every tip and the entire ring visible. Crisp high contrast black and white, square PNG suitable for downscaling as a favicon. No mockup, no shadow, no additional elements.
