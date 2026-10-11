# Firmware website

The site is generated from the repository's root `README.md`, `CHANGELOG.md`,
and Markdown files in `docs/`. Update those files normally; no separate website
copy needs maintaining. Every page links to <https://espterminator.com/> for web
flashing. This link opens ESPTerminator; it does not select or upload firmware.

The latest dated changelog entry supplies the release summary. `[Unreleased]`
remains in the changelog and is never presented as a released version. The README
version is read from `- **Version:** ...`. Keep dated changelog headings in the
format `## [1.2.3] - YYYY-MM-DD`, newest first.

## Automatic publishing with GitHub Pages

1. In the repository's **Settings → Pages → Build and deployment**, choose
   **GitHub Actions** as the source (one-time setup).
2. Commit and push `website/` and `.github/workflows/website.yml` along with your
   desired documentation changes to `main`.
3. The **Firmware website** action builds and publishes the site. Future pushes
   that change the README, changelog, docs, assets, or website rebuild it
   automatically. Pull requests check the build without publishing.

The Actions deployment shows the live URL. The default GitHub Pages address for
this repository is `https://dagnazty.github.io/awokxdag/`, unless a custom domain
is configured. The workflow can also be run manually from the Actions tab.
See [GitHub's custom workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

A private Sites preview, if published, is a snapshot for review; the GitHub Pages
workflow is what keeps the public website synchronized with future repository
updates. No Sites credentials or extra service secrets are required for Pages.

## Local preview

From the repository root:

```sh
npm ci --prefix website
npm test --prefix website
npm run build --prefix website
python3 -m http.server 4173 --directory website/dist
```

Open `http://localhost:4173`. Re-run the build after editing documentation.
Generated files live in `website/dist/` and are not committed.

The generated site contains static HTML and CSS, works without JavaScript,
uses relative URLs so it works under a repository subpath, and has no runtime
CDN or GitHub API dependency. Markdown is sanitized during the build. Links
between the README, changelog, and docs resolve to local pages; other repository
file links resolve to GitHub. Layout lives in `template.html` and `style.css`.
