"""Exercise the actual Sessions UI with isolated demo data and no real wallbox."""
import argparse
import json
from pathlib import Path
from playwright.sync_api import sync_playwright, expect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', required=True)
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    html = (app / 'assets/generated/demo.html').read_text(encoding='utf-8')
    output = app / 'build/verification'
    output.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=args.browser)
        context = browser.new_context(offline=True, locale='de-AT', timezone_id='Europe/Vienna')
        page = context.new_page()
        page.set_viewport_size({'width': 390, 'height': 844})
        errors, requests, sockets = [], [], []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('request', lambda request: requests.append(request.url))
        page.on('websocket', lambda socket: sockets.append(socket.url))
        page.set_content(html)
        expect(page.locator('#targetChargePower')).to_have_text('5,5')
        expect(page.locator('#sessionsPage')).to_be_hidden()
        page.evaluate("""() => {
          window.exports = [];
          window.InnoChargeHost = {postMessage: message => {
            exports.push(JSON.parse(message));
            window.dispatchEvent(new CustomEvent('innocharge-export-result', {detail: {ok:true, message:'Export test'}}));
          }};
          window.InnoChargeExportSupported = true;
          window.dispatchEvent(new Event('innocharge-export-ready'));
        }""")
        page.locator('#sessionsTab').click()
        expect(page.locator('#chargingPage')).to_be_hidden()
        expect(page.locator('#appSessionCount')).to_have_text('Showing 20 of 46 sessions')
        expect(page.locator('.session-card')).to_have_count(20)
        expect(page.locator('#sessionExport')).to_be_enabled()
        page.locator('#sessionExport').click()
        exported = page.evaluate('exports[0]')
        assert len(exported['sessions']) == 46, 'Export must include rows beyond the visible 20'
        assert [s['transactionId'] for s in exported['sessions']] == list(range(100, 54, -1))

        page.locator('#appSessionSort').select_option('energyWh')
        page.locator('#sessionExport').click()
        assert page.evaluate('exports.at(-1).sessions[0].transactionId') == 55
        page.locator('#sessionSortDirection').click()
        page.locator('#sessionExport').click()
        assert page.evaluate('exports.at(-1).sessions[0].transactionId') == 100
        page.locator('.session-filters summary').click()
        page.locator('#appSessionUser').fill('Maria')
        expect(page.locator('#appSessionCount')).to_have_text('Showing 15 of 15 sessions')
        page.locator('#sessionExport').click()
        assert len(page.evaluate('exports.at(-1).sessions')) == 15
        assert all(s['userName'] == 'Maria' for s in page.evaluate('exports.at(-1).sessions'))
        page.locator('#appSessionStatus').select_option('active')
        expect(page.locator('#appSessionCount')).to_have_text('Showing 0 of 0 sessions')
        expect(page.locator('#sessionExport')).to_be_disabled()
        page.locator('#sessionClearFilters').click()
        page.locator('#appSessionFrom').fill('2026-11-01')
        page.locator('#appSessionTo').fill('2026-11-16')
        page.locator('#sessionExport').click()
        dated = page.evaluate('exports.at(-1).sessions')
        assert dated and len(dated) < 46
        assert all('2026-11-01' <= s['startTime'][:10] <= '2026-11-16' for s in dated)
        page.locator('#sessionClearFilters').click()
        page.locator('#appSessionSort').select_option('userName')
        page.locator('#sessionLoadMore').click()
        expect(page.locator('.session-card')).to_have_count(40)
        page.locator('.session-card summary').first.click()
        expect(page.locator('.session-card .session-details').first).to_be_visible()
        for width, height in [(320, 640), (390, 844), (720, 1600)]:
            page.set_viewport_size({'width': width, 'height': height})
            page.evaluate('window.scrollTo(0, 0)')
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'), f'Horizontal overflow at {width}'
            page.screenshot(path=str(output / f'sessions-{width}x{height}.png'), full_page=True)
            page.screenshot(path=str(output / f'sessions-{width}x{height}-viewport.png'))
        page.set_viewport_size({'width': 390, 'height': 844})
        page.locator('#chargingTab').click()
        expect(page.locator('#targetChargePower')).to_have_text('5,5')
        page.locator('#sessionsTab').click()
        expect(page.locator('.session-card')).to_have_count(40)

        # A changed revision between pages must restart rather than silently omit rows.
        page.evaluate("""() => {
          window.injectedChange = false;
          window.addEventListener('innocharge-sessions-page', event => {
            if (event.detail.sessionOffset === 20 && !window.injectedChange) {
              window.injectedChange = true;
              event.detail.sessionRevision += 1;
            }
          });
        }""")
        page.locator('#sessionRefresh').click()
        expect(page.locator('#appSessionCount')).to_have_text('Showing 20 of 46 sessions')
        assert page.evaluate('window.injectedChange')
        page.locator('#sessionExport').click()
        assert len(page.evaluate('exports.at(-1).sessions')) == 46
        assert len({s['transactionId'] for s in page.evaluate('exports.at(-1).sessions')}) == 46
        assert not requests and not sockets, 'Offline demo must not create network traffic'
        assert not errors, errors

        # Session names are displayed as text, never interpreted as markup.
        unsafe = context.new_page()
        unsafe.set_content(html.replace("'Alex', 'Maria'", "'<img src=x onerror=window.__xss=true>', 'Maria'"))
        unsafe.locator('#sessionsTab').click()
        expect(unsafe.locator('#appSessionCount')).to_have_text('Showing 20 of 46 sessions')
        assert unsafe.locator('#appSessionList img').count() == 0
        assert unsafe.evaluate('window.__xss !== true')
        assert '<img src=x' in unsafe.locator('.session-user').first.inner_text()
        browser.close()
    print('PASS: complete-history sorting/filtering/export, paging, revision changes, responsive layout, charging navigation, and escaped text')


if __name__ == '__main__':
    main()
