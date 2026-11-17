"""Check icon navigation and clock settings in the isolated packaged app UI."""
import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright, expect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', required=True)
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    document = (app / 'assets/generated/demo.html').read_text(encoding='utf-8')
    output = app / 'build/verification'
    output.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as p:
        browser = p.chromium.launch(executable_path=args.browser)
        context = browser.new_context(offline=True, timezone_id='America/New_York')
        page = context.new_page()
        page.set_viewport_size(dict(width=390, height=844))
        errors, requests, sockets = [], [], []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('request', lambda request: requests.append(request.url))
        page.on('websocket', lambda socket: sockets.append(socket.url))
        page.set_content(document)
        expect(page.locator('#targetChargePower')).to_have_text('5,5')
        page.evaluate("""() => {
          window.clockCommands = [];
          window.dropTimeReply = false;
          const send = WebSocket.prototype.send;
          WebSocket.prototype.send = function(text) {
            const command = JSON.parse(text);
            clockCommands.push(command);
            if (window.dropTimeReply && command.action === 'setDeviceTime') return;
            return send.call(this, text);
          };
        }""")
        for name, selector in [('Charging', '#chargingTab'), ('Sessions', '#sessionsTab'), ('Settings', '#nativeMenu')]:
            assert page.locator(selector).inner_text().strip() == ''
            expect(page.locator(selector)).to_have_attribute('aria-label', name)
            expect(page.locator(selector + ' svg')).to_have_count(1)
        page.locator('#sessionsTab').click()
        expect(page.locator('#appSessionCount')).to_have_text('Showing 20 of 46 sessions')
        page.locator('#nativeMenu').click()
        expect(page.get_by_role('dialog', name='Settings')).to_be_visible()
        expect(page.locator('#appUsePhoneTime')).to_be_enabled()
        expect(page.locator('#appSwitchWallbox')).to_be_hidden()
        assert not page.evaluate("clockCommands.some(m => m.action === 'setDeviceTime')")
        field = page.locator('#appDeviceDateTime')
        field.fill('')
        page.locator('#appSaveTime').click()
        assert not page.evaluate("clockCommands.some(m => m.action === 'setDeviceTime')")

        # Manual values are wallbox-local, independent of the phone/browser timezone.
        field.fill('2026-11-17T12:34')
        page.locator('#appSaveTime').click()
        expect(page.locator('#appTimeMessage')).to_have_text('Time updated.')
        expect(page.locator('#appDeviceTime')).to_contain_text('12:34:')
        command = page.evaluate("clockCommands.find(m => m.action === 'setDeviceTime')")
        assert command['localDateTime'] == '2026-11-17T12:34:00'
        assert command['client'] == 'innocharge-public-app' and 'epoch' not in command
        field.fill('2026-11-17T15:00')
        page.evaluate("window.dispatchEvent(new CustomEvent('innocharge-time', {detail:{type:'appTime',epoch:1794915240,localDateTime:'2026-11-17T12:34:00'}}))")
        assert field.input_value() == '2026-11-17T15:00'

        # Phone time is an absolute epoch; no manual field is required.
        field.fill('')
        before = page.evaluate('Math.floor(Date.now()/1000)')
        page.locator('#appUsePhoneTime').click()
        expect(page.locator('#appSaveTime')).to_be_enabled()
        command = page.evaluate("clockCommands.filter(m => m.action === 'setDeviceTime').at(-1)")
        after = page.evaluate('Math.floor(Date.now()/1000)')
        assert before <= command['epoch'] <= after and 'localDateTime' not in command
        assert field.input_value()

        # Timeout and connection changes must leave no stuck saving state.
        page.evaluate('window.dropTimeReply = true')
        page.locator('#appUsePhoneTime').click()
        expect(page.locator('#appSaveTime')).to_be_disabled()
        expect(page.locator('#appTimeMessage')).to_have_text('No response. Please try again.', timeout=10000)
        expect(page.locator('#appUsePhoneTime')).to_be_enabled()
        page.evaluate("window.dispatchEvent(new CustomEvent('innocharge-connection',{detail:false}))")
        expect(page.locator('#appUsePhoneTime')).to_be_disabled()
        expect(page.locator('#appTimeMessage')).to_have_text('Connection lost. Please try again.')
        page.evaluate("window.dropTimeReply = false; window.dispatchEvent(new CustomEvent('innocharge-connection',{detail:true}))")
        expect(page.locator('#appUsePhoneTime')).to_be_enabled()

        for width, height in [(320,640),(390,844),(720,1000)]:
            page.set_viewport_size(dict(width=width,height=height))
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
            for selector in ('#appDeviceDateTime', '#appSaveTime', '#appUsePhoneTime', '.settings-panel'):
                box = page.locator(selector).bounding_box()
                assert box and box['x'] >= 0 and box['x'] + box['width'] <= width + 1, (width, selector, box)
            page.screenshot(path=str(output / f'app-settings-{width}.png'))
        page.set_viewport_size(dict(width=390,height=844))
        page.keyboard.press('Escape')
        expect(page.locator('#appSettings')).to_be_hidden()
        expect(page.locator('#nativeMenu')).to_be_focused()
        expect(page.locator('#sessionsPage')).to_be_visible()
        expect(page.locator('#sessionsTab')).to_have_attribute('aria-selected','true')

        # New wrappers return directly; older wrappers retain their existing menu fallback.
        page.evaluate("window.hostMessages=[]; window.InnoChargeHost={postMessage:m=>hostMessages.push(m)};window.InnoChargeNavigationSupported=true")
        page.locator('#nativeMenu').click()
        page.locator('#appSwitchWallbox').click()
        assert page.evaluate('hostMessages.at(-1)') == 'back'
        page.evaluate('window.InnoChargeNavigationSupported=false')
        page.locator('#nativeMenu').click()
        page.locator('#appSwitchWallbox').click()
        assert page.evaluate('hostMessages.at(-1)') == 'menu'
        page.locator('#chargingTab').click()
        page.screenshot(path=str(output / 'app-icon-navigation.png'))
        page.locator('#demoMaximum').select_option('22')
        expect(page.locator('#maxPowerLabel')).to_have_text('22 kW')
        page.locator('#nativeMenu').click()
        page.locator('#appReload').click()
        expect(page.locator('#appSettings')).to_be_hidden()
        expect(page.locator('#maxPowerLabel')).to_have_text('11 kW')
        expect(page.locator('#targetChargePower')).to_have_text('5,5')
        assert not errors, errors
        assert not requests and not sockets, (requests, sockets)
        print('PASS: icon-only navigation, clock read/manual/phone, UTC semantics, timeouts, reconnect, native back, responsive dialog, preserved Sessions and zero network.')
        context.close()
        browser.close()


if __name__ == '__main__':
    main()
