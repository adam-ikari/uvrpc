# UVRPC Documentation Website

This directory contains the VitePress-based documentation website for UVRPC.

## Setup

```bash
cd docs
npm install
```

## Development

```bash
cd docs
npm run docs:dev
```

The documentation will be available at `http://localhost:5173/`

## Build

```bash
cd docs
npm run docs:build
```

The built files will be in `.vitepress/dist/`

## Preview Production Build

```bash
cd docs
npm run docs:preview
```

## Directory Structure

```
docs/
├── .vitepress/          # VitePress config
│   └── config.mjs       # Site configuration
├── en/                  # English documentation
│   ├── index.md         # Home page
│   ├── quick-start.md   # Quick start guide
│   ├── guide/           # Guides
│   ├── api/             # API reference
│   ├── architecture/    # Architecture docs
│   └── development/     # Development docs
├── zh/                  # Chinese documentation
│   └── index.md         # Chinese home page
└── en/                  # Original English docs
    └── zh/              # Original Chinese docs
```

## Features

- 📚 Multi-language support (English/Chinese)
- 🔍 Full-text search
- 🎨 Modern responsive design
- 📱 Mobile-friendly
- ⚡ Fast page loads
- 🎯 Easy navigation

## Deploying

### GitHub Pages

1. Build the documentation:
   ```bash
   npm run docs:build
   ```

2. Deploy `.vitepress/dist/` to GitHub Pages

### Vercel

1. Connect your repository to Vercel
2. Set build command: `cd docs && npm run docs:build`
3. Set output directory: `docs/.vitepress/dist`

### Netlify

1. Connect your repository to Netlify
2. Set build command: `cd docs && npm run docs:build`
3. Set publish directory: `docs/.vitepress/dist`

## Customization

Edit `.vitepress/config.mjs` to customize:
- Site title and description
- Navigation
- Sidebar
- Theme
- Social links

## Adding Content

1. Add Markdown files to `en/` or `zh/` directories
2. Update `.vitepress/config.mjs` to add them to navigation/sidebar
3. Run `npm run docs:dev` to preview

## License

MIT