"""Quick E2E test for the Flask webapp."""
from playwright.sync_api import sync_playwright
import time
import sys

def main():
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1920, "height": 1080})

        print("[E2E] Navigating to Flask app...")
        page.goto("http://localhost:5000", timeout=30000)
        time.sleep(3)

        print("[E2E] Waiting for auto-load...")
        time.sleep(8)

        page.screenshot(path="tests/e2e_flask_initial.png")
        print("[E2E] Screenshot: tests/e2e_flask_initial.png")

        status = page.locator("#status-text").inner_text()
        print(f"[E2E] Status: {status}")

        canvases = page.locator("canvas").all()
        print(f"[E2E] Canvas elements: {len(canvases)}")

        mode = page.locator("#var-mode").inner_text()
        vbat = page.locator("#var-vbat").inner_text()
        soc = page.locator("#var-soc").inner_text()
        print(f"[E2E] Mode: {mode}, Vbat: {vbat}, SOC: {soc}")

        print("[E2E] Clicking Play...")
        page.locator("#btn-play").click()
        time.sleep(3)

        page.screenshot(path="tests/e2e_flask_playing.png")
        clock = page.locator("#clock-value").inner_text()
        print(f"[E2E] Clock after play: {clock}")

        print("[E2E] Loading scenario 10...")
        page.locator("#scenario-select").select_option("10")
        page.locator("#btn-load").click()
        time.sleep(5)

        page.screenshot(path="tests/e2e_flask_scenario10.png")
        status10 = page.locator("#status-text").inner_text()
        print(f"[E2E] Scenario 10: {status10}")

        browser.close()

        if "loaded" in status.lower() or "data points" in status.lower():
            print("[E2E] ALL TESTS PASSED")
            return 0
        else:
            print("[E2E] TESTS FAILED - scenario didn't load")
            return 1

if __name__ == "__main__":
    sys.exit(main())
