import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const { Resvg } = require("../tools/ui-assets/node_modules/@resvg/resvg-js");
const { PNG } = require("../tools/ui-assets/node_modules/pngjs");

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const OUT_H = path.join(ROOT, "src", "assets", "icon_assets.h");
const OUT_C = path.join(ROOT, "src", "assets", "icon_assets.c");

const APP_SIZES = [24, 32, 48];
const SYMBOL_SIZES = [16, 20, 24];

const assets = [
  {
    id: "ICON_APP_TERMINAL",
    kind: "app",
    source: "assets/upstream/icons/papirus/utilities-terminal.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_FILES",
    kind: "app",
    source: "assets/upstream/icons/papirus/system-file-manager.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_EDITOR",
    kind: "app",
    source: "assets/upstream/icons/papirus/accessories-text-editor.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_OSINFO",
    kind: "app",
    source: "assets/upstream/icons/papirus/dialog-information.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_SETTINGS",
    kind: "app",
    source: "assets/upstream/icons/papirus/preferences-system.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_TASKMGR",
    kind: "app",
    source: "assets/upstream/icons/papirus/utilities-system-monitor.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_SNAKE",
    kind: "app",
    source: "assets/upstream/icons/papirus/applications-games.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_NOTES",
    kind: "app",
    source: "assets/upstream/icons/papirus/notes.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_STORE",
    kind: "app",
    source: "assets/upstream/icons/papirus/softwarecenter.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_CALC",
    kind: "app",
    source: "assets/upstream/icons/papirus/accessories-calculator.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_BROWSER",
    kind: "app",
    source: "assets/upstream/icons/papirus/web-browser.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_AXDOCS",
    kind: "app",
    source: "assets/upstream/icons/papirus/document.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_AXSTUDIO",
    kind: "app",
    source: "assets/upstream/icons/papirus/applications-development.svg",
    sizes: APP_SIZES,
  },
  {
    id: "ICON_APP_WORK180",
    kind: "app",
    source: "assets/upstream/icons/papirus/x-office-presentation.svg",
    sizes: APP_SIZES,
  },

  {
    id: "ICON_SYM_POWER",
    kind: "symbol",
    source: "assets/upstream/icons/custom/power.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_USER",
    kind: "symbol",
    source: "assets/upstream/icons/custom/user.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_SEARCH",
    kind: "symbol",
    source: "assets/upstream/icons/custom/search.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_LOGOUT",
    kind: "symbol",
    source: "assets/upstream/icons/fluent/arrow-exit-24.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_ADD_USER",
    kind: "symbol",
    source: "assets/upstream/icons/fluent/add-24.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_CLOSE",
    kind: "symbol",
    source: "assets/upstream/icons/fluent/dismiss-24.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_MINIMIZE",
    kind: "symbol",
    source: "assets/upstream/icons/custom/minimize.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_MAXIMIZE",
    kind: "symbol",
    source: "assets/upstream/icons/custom/maximize.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_RESTORE",
    kind: "symbol",
    source: "assets/upstream/icons/custom/restore.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_FOLDER",
    kind: "symbol",
    source: "assets/upstream/icons/fluent/folder-24.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_DOCUMENT",
    kind: "symbol",
    source: "assets/upstream/icons/fluent/document-24.svg",
    sizes: SYMBOL_SIZES,
  },
  {
    id: "ICON_SYM_SETTINGS",
    kind: "symbol",
    source: "assets/upstream/icons/fluent/apps-settings-20.svg",
    sizes: SYMBOL_SIZES,
  },
];

function bytesToC(bytes, indent = "    ", wrap = 16) {
  const lines = [];
  for (let i = 0; i < bytes.length; i += wrap) {
    const chunk = bytes
      .slice(i, i + wrap)
      .map((v) => `0x${v.toString(16).padStart(2, "0").toUpperCase()}`)
      .join(", ");
    lines.push(`${indent}${chunk},`);
  }
  if (!lines.length) lines.push(`${indent}0x00,`);
  return lines.join("\n");
}

function renderVariant(sourcePath, size, kind) {
  const svg = fs.readFileSync(sourcePath, "utf8");
  const png = new Resvg(svg, {
    fitTo: { mode: "width", value: size },
  }).render().asPng();
  const decoded = PNG.sync.read(png);
  const bytes = [];
  if (kind === "symbol") {
    for (let i = 0; i < decoded.data.length; i += 4) {
      bytes.push(decoded.data[i + 3]);
    }
  } else {
    bytes.push(...decoded.data);
  }
  return {
    width: decoded.width,
    height: decoded.height,
    bytes,
  };
}

function main() {
  fs.mkdirSync(path.dirname(OUT_H), { recursive: true });

  const header = `#pragma once

#include <stdint.h>

#include "drivers/icon.h"

typedef enum {
    ICON_ASSET_ALPHA = 0,
    ICON_ASSET_RGBA = 1,
} icon_asset_format_t;

typedef struct {
    icon_asset_id_t id;
    uint8_t pixel_size;
    uint8_t width;
    uint8_t height;
    uint8_t format;
    const uint8_t *pixels;
} icon_asset_variant_t;

extern const icon_asset_variant_t g_icon_asset_variants[];
extern const int g_icon_asset_variant_count;
`;

  const body = [
    '#include "assets/icon_assets.h"',
    "",
    "/* Generated by scripts/generate_icon_assets.mjs. */",
    "",
  ];
  const variants = [];

  for (const asset of assets) {
    const fullSource = path.join(ROOT, asset.source);
    for (const size of asset.sizes) {
      const rendered = renderVariant(fullSource, size, asset.kind);
      const symbol = `${asset.id.toLowerCase().replace(/[^a-z0-9]+/g, "_")}_${size}`;
      const arrayType = asset.kind === "symbol" ? "uint8_t" : "uint8_t";
      body.push(`static const ${arrayType} k_icon_${symbol}[] = {`);
      body.push(bytesToC(rendered.bytes));
      body.push("};");
      body.push("");
      variants.push(
        `    { ${asset.id}, ${size}, ${rendered.width}, ${rendered.height}, ${
          asset.kind === "symbol" ? "ICON_ASSET_ALPHA" : "ICON_ASSET_RGBA"
        }, k_icon_${symbol} },`
      );
    }
  }

  body.push("const icon_asset_variant_t g_icon_asset_variants[] = {");
  body.push(...variants);
  body.push("};");
  body.push("");
  body.push(
    "const int g_icon_asset_variant_count = (int)(sizeof(g_icon_asset_variants) / sizeof(g_icon_asset_variants[0]));"
  );
  body.push("");

  fs.writeFileSync(OUT_H, header);
  fs.writeFileSync(OUT_C, body.join("\n"));
  console.log(`Wrote ${path.relative(ROOT, OUT_H)} and ${path.relative(ROOT, OUT_C)}`);
}

main();
