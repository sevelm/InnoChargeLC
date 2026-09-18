"""Render SVG artwork without changing its colors or proportions.

Build-time dependency: pip install playwright && playwright install chromium.
Alternatively pass --browser with the path to an installed Chrome executable.
Generated assets are committed; normal Flutter builds do not need this tool.
"""

import argparse
import base64
from pathlib import Path
import xml.etree.ElementTree as ET

from playwright.sync_api import sync_playwright


def svg_url(path):
    root = ET.parse(path).getroot()
    ET.register_namespace('', 'http://www.w3.org/2000/svg')
    ET.register_namespace('xlink', 'http://www.w3.org/1999/xlink')
    return 'data:image/svg+xml;base64,' + base64.b64encode(ET.tostring(root)).decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser')
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    data = app.parent / 'data'
    assets = app / 'assets/generated'
    assets.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=args.browser)
        page = browser.new_page()

        def render(source, width, height, padding=0, background=None):
            encoded = page.evaluate('''async (options) => {
                const image = new Image();
                image.src = options.source;
                await image.decode();
                const canvas = document.createElement('canvas');
                canvas.width = options.width;
                canvas.height = options.height;
                const ctx = canvas.getContext('2d');
                if (options.background) {
                    ctx.fillStyle = options.background;
                    ctx.fillRect(0, 0, canvas.width, canvas.height);
                }
                const scale = Math.min(
                    (canvas.width - options.padding * 2) / image.naturalWidth,
                    (canvas.height - options.padding * 2) / image.naturalHeight);
                const w = image.naturalWidth * scale;
                const h = image.naturalHeight * scale;
                ctx.drawImage(image, (canvas.width - w) / 2,
                    (canvas.height - h) / 2, w, h);
                return canvas.toDataURL('image/png').split(',')[1];
            }''', dict(source=svg_url(source), width=width, height=height,
                      padding=padding, background=background))
            return base64.b64decode(encoded)

        wordmark = render(data / 'innocharge.svg', 1600, 382)
        (assets / 'innocharge.png').write_bytes(wordmark)
        icon = data / 'innologo.svg'
        (assets / 'app_icon.png').write_bytes(
            render(icon, 1024, 1024, padding=224, background='#263238'))
        (assets / 'app_icon_foreground.png').write_bytes(render(icon, 1024, 1024))
        # Keep the existing browser favicon URL, generated from the same artwork.
        (data / 'favicon.ico').write_bytes(render(icon, 96, 96, padding=16, background='#263238'))
        browser.close()
    print('Generated transparent wordmark and square launcher artwork from SVG.')


if __name__ == '__main__':
    main()
