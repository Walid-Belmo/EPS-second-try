"""
Debug test: check clock, slider, and data consistency for all scenarios.
Run with: python tests/e2e_flask_debug.py
Requires Flask server running on localhost:5000
"""
from playwright.sync_api import sync_playwright
import time
import sys
import json

SCENARIOS_EXPECTED_DURATION = {
    1: 99.8,
    2: 99.8,
    3: 99.8,
    5: 199.8,
    6: 99.8,
    9: 199.8,
    10: 599.8,
    11: 299.8,
    12: 299.8,
}

def main():
    errors = []

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1920, "height": 1080})

        page.goto("http://localhost:5000", timeout=30000)
        time.sleep(5)

        for scenario, expected_duration in SCENARIOS_EXPECTED_DURATION.items():
            print(f"\n{'='*60}")
            print(f"Testing Scenario {scenario} (expected {expected_duration}s)")
            print(f"{'='*60}")

            # Select and load scenario
            page.locator("#scenario-select").select_option(str(scenario))
            page.locator("#btn-load").click()
            time.sleep(5)

            # Read status bar
            status = page.locator("#status-text").inner_text()
            print(f"  Status: {status}")

            # Read slider right label (total time)
            slider_right = page.locator("#slider-time-right").inner_text()
            print(f"  Slider right label: {slider_right}")

            # Read clock
            clock = page.locator("#clock-value").inner_text()
            print(f"  Clock: {clock}")

            # Read slider max attribute
            slider_max = page.locator("#time-slider").get_attribute("max")
            print(f"  Slider max: {slider_max}")

            # Read slider value
            slider_val = page.locator("#time-slider").get_attribute("value")
            print(f"  Slider value: {slider_val}")

            # Get data length from JS
            data_info = page.evaluate("""() => {
                return {
                    length: simData.length,
                    firstT: simData.length > 0 ? simData[0].t : null,
                    lastT: simData.length > 0 ? simData[simData.length-1].t : null,
                    currentIndex: currentIndex
                };
            }""")
            print(f"  JS simData.length: {data_info['length']}")
            print(f"  JS first t: {data_info['firstT']}")
            print(f"  JS last t: {data_info['lastT']}")
            print(f"  JS currentIndex: {data_info['currentIndex']}")

            # Verify last t matches expected duration
            if data_info['lastT'] is not None:
                diff = abs(data_info['lastT'] - expected_duration)
                if diff > 1.0:
                    msg = f"  ERROR: Scenario {scenario} last t={data_info['lastT']}, expected {expected_duration}"
                    print(msg)
                    errors.append(msg)
                else:
                    print(f"  OK: last t matches expected duration")

            # Click Play, wait 2 seconds, check clock advanced
            page.locator("#btn-play").click()
            time.sleep(2)

            clock_after = page.locator("#clock-value").inner_text()
            print(f"  Clock after 2s play: {clock_after}")

            # Pause
            page.locator("#btn-play").click()
            time.sleep(0.5)

            # Move slider to end
            page.locator("#time-slider").evaluate(
                "el => { el.value = el.max; el.dispatchEvent(new Event('input')); }"
            )
            time.sleep(1)

            clock_at_end = page.locator("#clock-value").inner_text()
            slider_right_end = page.locator("#slider-time-right").inner_text()
            print(f"  Clock at slider end: {clock_at_end}")
            print(f"  Slider right at end: {slider_right_end}")

            # Verify clock at end matches expected duration
            expected_min = int(expected_duration // 60)
            expected_sec = int(expected_duration % 60)
            expected_clock = f"{expected_min:02d}:{expected_sec:02d}"
            print(f"  Expected clock at end: ~{expected_clock}")

            page.screenshot(path=f"tests/e2e_debug_s{scenario}.png")

        browser.close()

    print(f"\n{'='*60}")
    if errors:
        print(f"ERRORS FOUND: {len(errors)}")
        for e in errors:
            print(f"  {e}")
        return 1
    else:
        print("ALL CHECKS PASSED")
        return 0

if __name__ == "__main__":
    sys.exit(main())
