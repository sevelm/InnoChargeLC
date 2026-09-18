"""Exercise the packaged offline demo, including CSP and responsive screenshots."""

import argparse
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageChops
from playwright.sync_api import sync_playwright, expect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser')
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    html = (app / 'assets/generated/demo.html').read_text(encoding='utf-8')
    screenshots = app / 'build/verification'
    screenshots.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=args.browser)
        context = browser.new_context(offline=True)
        page = context.new_page()
        errors, requests, sockets = [], [], []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('request', lambda request: requests.append(request.url))
        page.on('websocket', lambda socket: sockets.append(socket.url))
        page.set_content(html)
        expect(page.locator('#targetChargePower')).to_have_text('5,5')
        expect(page.locator('#connection')).to_have_text('simulation')
        expect(page.locator('#nativeMenu')).to_be_hidden()
        expect(page.locator('#powerRing')).to_have_attribute('data-power-percent', '50')

        for width, height in ((320, 640), (390, 844), (720, 1600), (1440, 900)):
            page.set_viewport_size({'width': width, 'height': height})
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
            for selector in ('.brand svg', '#powerRing', '#demoState', '#demoMaximum', '#demoPhase', '.stepper'):
                bounds = page.locator(selector).bounding_box()
                assert bounds and bounds['width'] > 0
                assert bounds['x'] >= 0 and bounds['x'] + bounds['width'] <= width + 1
            logo = Image.open(BytesIO(page.locator('.brand svg').screenshot())).convert('RGB')
            colors = {color: count for count, color in logo.getcolors(logo.width * logo.height)}
            assert colors.get((88, 143, 199), 0) > 30
            assert colors.get((255, 255, 255), 0) > 30
            page.screenshot(path=str(screenshots / f'demo-{width}x{height}.png'), full_page=True)

        page.set_viewport_size({'width': 390, 'height': 844})
        ring = page.locator('#powerRing')
        first = Image.open(BytesIO(ring.screenshot())).convert('RGB')
        page.wait_for_timeout(700)
        second = Image.open(BytesIO(ring.screenshot())).convert('RGB')
        assert ImageChops.difference(first, second).getbbox(), 'Charge animation did not move'

        for state, caption, color, animation in (
            ('A', 'Ready to connect', '#588fc7', 'solid'),
            ('B', 'Vehicle connected', '#d5ad70', 'solid'),
            ('C', 'Charging', '#75b995', 'charge'),
            ('D', 'Charging - ventilation', '#75b995', 'charge'),
            ('paused', 'Charging paused', '#ad91c9', 'solid'),
            ('authorization', 'Waiting for authorization', '#d5ad70', 'wave'),
            ('E', 'Charging error', '#d77e83', 'solid'),
            ('F', 'Charger fault', '#d77e83', 'blink'),
        ):
            page.select_option('#demoState', state)
            expect(page.locator('#chargeStatus')).to_have_text(caption)
            expect(ring).to_have_attribute('data-animation', animation)
            assert ring.evaluate("el => el.style.getPropertyValue('--state-color')") == color
            expect(page.locator('#chargingActive')).to_have_text('yes' if state in ('C', 'D') else 'no')

        page.select_option('#demoState', 'C')
        field = page.locator('#setChargePower')
        for maximum in (11, 22):
            page.select_option('#demoMaximum', str(maximum))
            expect(page.locator('#powerRange')).to_have_attribute('max', str(maximum))
            for power, percent in ((0, '0'), (maximum / 2, '50'), (maximum, '100')):
                field.fill(str(power))
                field.press('Tab')
                expect(ring).to_have_attribute('data-power-percent', percent)
                expect(page.locator('#chargeStatus')).to_have_text('Charging paused' if power == 0 else 'Charging')
        page.select_option('#demoMaximum', '11')
        expect(field).to_have_value('11.0')
        expect(page.locator('#targetChargePower')).to_have_text('11,0')
        field.fill('12')
        field.press('Tab')
        expect(field).to_have_attribute('aria-invalid', 'true')
        expect(page.locator('#targetChargePower')).to_have_text('11,0')
        field.fill('0.5')
        field.press('Tab')
        expect(page.locator('#targetChargePower')).to_have_text('1,4')
        page.select_option('#demoPhase', 'Single-phase')
        expect(page.locator('#phaseMode')).to_have_text('Single-phase')
        page.locator('#increasePower').click()
        expect(field).to_have_value('0.6')
        page.locator('#powerRange').fill('5.5')
        page.locator('#powerRange').dispatch_event('change')
        expect(page.locator('#targetChargePower')).to_have_text('5,5')
        page.wait_for_timeout(16000)
        expect(page.locator('#powerControls')).to_be_enabled()
        assert not errors, errors
        assert not requests, requests
        assert not sockets, sockets
        assert page.evaluate("Object.getOwnPropertyDescriptor(window, 'WebSocket').writable") is False
        assert page.evaluate("fetch('https://example.invalid').then(() => false, () => true)")

        # Reset like Flutter's reload action: new document, no retained settings.
        page.goto('about:blank')
        page.set_content(html)
        expect(page.locator('#targetChargePower')).to_have_text('5,5')
        expect(page.locator('#phaseMode')).to_have_text('Three-phase')
        expect(page.locator('#demoMaximum')).to_have_value('11')
        page.emulate_media(reduced_motion='reduce')
        assert page.locator('.ring-wave').evaluate("el => getComputedStyle(el).display") == 'none'
        browser.close()
    print('PASS: offline demo, states, controls, limits, heartbeat, reset, CSP, logos, animation and 4 viewports')


if __name__ == '__main__':
    main()
