"""Browser regression checks for web branding/app; requires Playwright and Pillow."""

import argparse
from io import BytesIO
import mimetypes
from pathlib import Path
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

from PIL import Image
from playwright.sync_api import sync_playwright


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser')
    parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    # Generated files are derived assets; only two logo originals remain in data/.
    assert sorted(p.name for p in (repo / 'data').iterdir() if p.name.lower().startswith('inno')) == ['innocharge.svg', 'innologo.svg']
    icon = Image.open(repo / 'mobile_app/assets/generated/app_icon.png').convert('RGB')
    assert icon.size == (1024, 1024)
    white = icon.point(lambda value: 255 if value == 255 else 0).convert('L')
    box = white.getbbox()
    assert box and box[0] >= 290 and 224 <= box[1] <= 226 and box[2] <= 734 and 798 <= box[3] <= 800, box
    adaptive = Image.open(repo / 'mobile_app/android/app/src/main/res/drawable-xxxhdpi/ic_launcher_foreground.png').convert('RGBA')
    box = adaptive.getchannel('A').getbbox()
    config = ET.parse(repo / 'mobile_app/android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml')
    inset = float(config.find('.//inset').get('{http://schemas.android.com/apk/res/android}inset').rstrip('%')) / 100
    assert inset == 0.26 and box
    # Android applies the XML inset at runtime, not to the foreground PNG itself.
    for x in (box[0] / adaptive.width, box[2] / adaptive.width):
        for y in (box[1] / adaptive.height, box[3] / adaptive.height):
            dx = inset + x * (1 - inset * 2) - 0.5
            dy = inset + y * (1 - inset * 2) - 0.5
            assert dx * dx + dy * dy < (1 / 3) ** 2, 'Artwork exceeds the safe circular mask'
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=args.browser)
        context = browser.new_context(viewport={'width': 390, 'height': 844})

        def local_resource(route):
            path = (repo / 'data' / unquote(urlsplit(route.request.url).path).lstrip('/')).resolve()
            if not path.is_relative_to(repo / 'data') or not path.is_file():
                route.fulfill(status=404, body='Not found')
                return
            route.fulfill(body=path.read_bytes(), content_type=mimetypes.guess_type(path)[0] or 'application/octet-stream')

        context.route('http://innocharge.test/**', local_resource)
        context.add_init_script('''window.WebSocket = class {
            static OPEN = 1; readyState = 1; send() {} close() {}
        }; window.print = () => { window.printRequested = true; };''')
        page = context.new_page()
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.goto('http://innocharge.test/app.html')
        page.evaluate('''() => {
            window.sent = [];
            socket = {readyState: WebSocket.OPEN,
                send: text => window.sent.push(JSON.parse(text)), close: () => {}};
            window.InnoChargeHost = {postMessage: () => {}};
            document.getElementById('nativeMenu').hidden = false;
            setConnection(true);
        }''')

        def update(power=5.5, maximum=11, animation='solid', color='#ffc300',
                   label='Vehicle connected', wave='#ffffff', period=2800):
            page.evaluate('processUpdate', dict(
                wallboxName='InnoCharge', cpState='State C' if animation == 'charge' else 'State B', phaseMode='Three-phase',
                vehicleConnected=True, chargingActive=animation == 'charge',
                targetChargePower=power, maxChargePower=maximum,
                ledStatus=dict(label=label, color=color, waveColor=wave,
                               animation=animation, periodMs=period)))

        update()
        assert page.locator('#wallboxName').is_hidden()
        assert page.locator('#powerRange').get_attribute('max') == '11'
        assert page.locator('#chargeStatus').inner_text() == 'Vehicle connected'
        def check_logo(locator, ink):
            pixels = Image.open(BytesIO(locator.screenshot())).convert('RGB')
            colors = {color: count for count, color in pixels.getcolors(pixels.width * pixels.height)}
            assert colors.get((88, 143, 199), 0) > 50, 'Missing logo blue'
            assert colors.get(ink, 0) > 50, 'Missing themed logo lettering'

        check_logo(page.locator('.brand svg'), (255, 255, 255))
        assert page.locator('#powerRing').get_attribute('data-power-percent') == '50'
        assert page.locator('#ringProgress').get_attribute('stroke-dasharray') == '50 50'
        for maximum in (11, 22):
            for power, expected in ((0, '0'), (maximum / 2, '50'), (maximum, '100')):
                update(power, maximum)
                assert page.locator('#powerRing').get_attribute('data-power-percent') == expected
            assert page.locator('#setChargePower').get_attribute('max') == str(maximum)
            page.locator('#setChargePower').fill(str(maximum + 0.1))
            before = page.evaluate('sent.length')
            page.evaluate('commitPower()')
            assert page.evaluate('sent.length') == before
            assert page.locator('#setChargePower').get_attribute('aria-invalid') == 'true'
            page.locator('#setChargePower').fill(str(maximum))
            page.evaluate('commitPower()')
            assert page.evaluate('sent.at(-1).power') == maximum
            assert page.locator('#increasePower').is_disabled()
            assert page.locator('#message').inner_text() == ''
        update(maximum=11)
        assert page.locator('#setChargePower').input_value() == '11.0'
        page.locator('#powerRange').fill('5.5')
        page.locator('#powerRange').dispatch_event('change')
        assert page.evaluate('sent.at(-1).power') == 5.5
        page.locator('#powerRange').blur()

        if args.screenshots:
            args.screenshots.mkdir(parents=True, exist_ok=True)
        for animation, label, color, display in (
            ('solid', 'Ready to connect', '#0000ff', '#588fc7'),
            ('solid', 'Vehicle connected', '#ffc300', '#d5ad70'),
            ('charge', 'Charging', '#00ff00', '#75b995'),
            ('wave', 'Waiting for authorization', '#ffc300', '#d5ad70'),
            ('blink', 'Charger fault', '#ff0000', '#d77e83'),
            ('solid', 'Charging paused', '#ff00ff', '#ad91c9'),
        ):
            update(animation=animation, label=label, color=color, wave=color)
            assert page.locator('#chargeStatus').inner_text() == label
            assert page.locator('#powerRing').get_attribute('data-animation') == animation
            assert page.locator('#powerRing').evaluate('(e) => e.style.getPropertyValue("--state-color")') == display
            assert page.locator('#powerRing').evaluate('(e) => e.style.getPropertyValue("--wave-color")') == display
            if args.screenshots:
                page.screenshot(path=str(args.screenshots / f'state-{label.replace(" ", "-")}.png'))

        assert page.evaluate("displayLedColor('#FFFFFF')") == '#dce5eb'
        assert page.evaluate("displayLedColor('#5580aa')") == '#70879e'
        assert not page.evaluate("applyLedStatus({label: 'Bad', color: 'red', waveColor: '#ffffff', animation: 'solid'})")

        update(animation='charge', color='#00ff00', label='Charging', wave='#00ff00')
        wave = page.locator('.ring-wave')
        first = wave.evaluate('(e) => getComputedStyle(e).strokeDashoffset')
        page.wait_for_timeout(250)
        assert wave.evaluate('(e) => getComputedStyle(e).strokeDashoffset') != first
        assert wave.evaluate('(e) => getComputedStyle(e).maskImage') != 'none'
        if args.screenshots:
            args.screenshots.mkdir(parents=True, exist_ok=True)
        for width, height in ((320, 640), (360, 760), (390, 844), (844, 390), (1440, 900)):
            page.set_viewport_size(dict(width=width, height=height))
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
            if width < height:
                assert page.locator('.range-labels').bounding_box()['y'] + 20 <= height
            assert page.locator('#chargeStatus').evaluate('''e => {
                const a = e.getBoundingClientRect();
                const b = document.getElementById('powerRing').getBoundingClientRect();
                return a.bottom < b.bottom && a.left >= b.left && a.right <= b.right;
            }''')
            if args.screenshots:
                page.screenshot(path=str(args.screenshots / f'app-{width}x{height}.png'), full_page=True)

        page.emulate_media(reduced_motion='reduce')
        assert wave.evaluate('(e) => getComputedStyle(e).display') == 'none'
        page.evaluate('setConnection(false)')
        assert page.locator('#powerRange').is_disabled()
        assert page.locator('#chargeStatus').inner_text() == 'Offline'
        assert page.locator('#powerRing').get_attribute('data-power-percent') == '0'
        page.evaluate('processUpdate({targetChargePower: 5.5})')
        assert page.locator('#powerRange').is_disabled()
        assert page.locator('#chargeStatus').inner_text() == 'Status unavailable'
        update()
        assert page.locator('#powerRange').is_enabled()
        assert page.locator('#message').inner_text() == ''

        # The same SVG must stay visible on dark navigation and light login/print pages.
        page.goto('http://innocharge.test/login.html')
        check_logo(page.locator('.login-logo'), (38, 50, 56))
        if args.screenshots:
            page.screenshot(path=str(args.screenshots / 'login.png'))
        for width in (320, 390):
            page.set_viewport_size(dict(width=width, height=844))
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'), 'Login overflow'
            check_logo(page.locator('.login-logo'), (38, 50, 56))
        page.set_viewport_size(dict(width=1440, height=900))
        page.goto('http://innocharge.test/index.html')
        page.wait_for_selector('.logo use')
        check_logo(page.locator('.logo'), (255, 255, 255))
        page.goto('http://innocharge.test/sessions.html')
        with context.expect_page() as popup:
            page.evaluate('exportSessionsPdf()')
        report = popup.value
        report.wait_for_load_state()
        if args.screenshots:
            report.screenshot(path=str(args.screenshots / 'print-report.png'))
        check_logo(report.locator('.brand svg'), (38, 50, 56))
        report.emulate_media(media='print')
        check_logo(report.locator('.brand svg'), (38, 50, 56))
        if args.screenshots:
            report.screenshot(path=str(args.screenshots / 'print-report.png'))
        context.route('http://innocharge.test/innocharge.svg', lambda route: route.fulfill(status=503, body='Unavailable'))
        with context.expect_page() as fallback_popup:
            page.evaluate('exportSessionsPdf()')
        fallback_report = fallback_popup.value
        assert fallback_report.locator('.brand strong').inner_text() == 'InnoCharge'
        assert not errors, errors
        browser.close()
    print('PASS: themed SVGs (app/login/navigation/print), soft palette, 11/22 kW limits, ring animation, controls, offline, five viewports.')


if __name__ == '__main__':
    main()
