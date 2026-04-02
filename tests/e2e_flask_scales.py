"""
Test that chart axis scales are FIXED after loading (don't change during playback).
Also verify clock, duration, and data consistency.
"""
from playwright.sync_api import sync_playwright
import time
import sys
import json

def main():
    errors = []

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1920, "height": 1080})

        # Enable console log capture
        console_logs = []
        page.on("console", lambda msg: console_logs.append(msg.text))

        page.goto("http://localhost:5000", timeout=30000)
        time.sleep(5)

        # Test scenario 2 (eclipse entry) - has both charging and discharge phases
        print("Loading scenario 2...")
        page.locator("#scenario-select").select_option("2")
        page.locator("#btn-load").click()
        time.sleep(5)

        # Print all console logs
        print("\nConsole logs from browser:")
        for log in console_logs:
            if "[EPS]" in log:
                print(f"  {log}")
        console_logs.clear()

        # Check that axis ranges were computed
        ranges = page.evaluate("() => axisRanges")
        print(f"\nFixed axis ranges: {json.dumps(ranges, indent=2)}")

        if not ranges:
            errors.append("axisRanges is empty!")
        else:
            print(f"  vbat range: {ranges['vbat']['min']:.0f} - {ranges['vbat']['max']:.0f} mV")
            print(f"  soc range: {ranges['soc']['min']:.2f} - {ranges['soc']['max']:.2f} %")
            print(f"  ibat range: {ranges['ibat']['min']:.0f} - {ranges['ibat']['max']:.0f} mA")

        # Play for 3 seconds, capture chart scale at start
        print("\nPlaying for 3 seconds...")
        page.locator("#btn-play").click()
        time.sleep(1)

        # Check chart y-axis scale mid-playback
        vbat_scale_1 = page.evaluate("""() => ({
            min: chartVbat.options.scales.y.min,
            max: chartVbat.options.scales.y.max
        })""")
        print(f"  Vbat scale at t=1s: {vbat_scale_1}")

        time.sleep(2)

        vbat_scale_2 = page.evaluate("""() => ({
            min: chartVbat.options.scales.y.min,
            max: chartVbat.options.scales.y.max
        })""")
        print(f"  Vbat scale at t=3s: {vbat_scale_2}")

        # Verify scales didn't change
        if vbat_scale_1['min'] != vbat_scale_2['min'] or vbat_scale_1['max'] != vbat_scale_2['max']:
            errors.append(f"Vbat scale CHANGED during playback: {vbat_scale_1} -> {vbat_scale_2}")
            print("  ERROR: Scale changed!")
        else:
            print("  OK: Scale stayed fixed")

        page.locator("#btn-play").click()  # pause
        time.sleep(0.5)

        # Screenshot
        page.screenshot(path="tests/e2e_flask_fixed_scales.png")
        print("\nScreenshot: tests/e2e_flask_fixed_scales.png")

        # Test scenario 10 (long eclipse) to verify different ranges
        print("\nLoading scenario 10...")
        page.locator("#scenario-select").select_option("10")
        page.locator("#btn-load").click()
        time.sleep(5)

        for log in console_logs:
            if "[EPS]" in log:
                print(f"  {log}")
        console_logs.clear()

        ranges10 = page.evaluate("() => axisRanges")
        print(f"  Scenario 10 vbat range: {ranges10['vbat']['min']:.0f} - {ranges10['vbat']['max']:.0f} mV")
        print(f"  Scenario 10 soc range: {ranges10['soc']['min']:.2f} - {ranges10['soc']['max']:.2f} %")
        print(f"  Scenario 10 time range: {ranges10['time']['min']:.0f} - {ranges10['time']['max']:.0f} s")

        # Verify time range is correct (should be ~600s)
        if abs(ranges10['time']['max'] - 599.8) > 1.0:
            errors.append(f"Scenario 10 time max={ranges10['time']['max']}, expected ~599.8")

        # Move slider to end and check clock
        page.locator("#time-slider").evaluate(
            "el => { el.value = el.max; el.dispatchEvent(new Event('input')); }"
        )
        time.sleep(1)

        clock_end = page.locator("#clock-value").inner_text()
        print(f"  Clock at end: {clock_end}")
        if not clock_end.startswith("09:"):
            errors.append(f"Scenario 10 clock at end={clock_end}, expected 09:5x")

        page.screenshot(path="tests/e2e_flask_scenario10_end.png")

        browser.close()

    print(f"\n{'='*60}")
    if errors:
        print(f"ERRORS ({len(errors)}):")
        for e in errors:
            print(f"  {e}")
        return 1
    else:
        print("ALL SCALE AND TIMING TESTS PASSED")
        return 0

if __name__ == "__main__":
    sys.exit(main())
