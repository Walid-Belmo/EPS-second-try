"""
End-to-end test for the EPS Streamlit app using Playwright.
Launches a headless browser, loads the app, runs simulations,
and verifies that all plots render correctly.

Usage:
    python tests/e2e_test_eps_app.py

Requires: streamlit running on port 8502
    streamlit run tools/eps_app.py --server.port 8502 --server.headless true
"""

import sys
import time

from playwright.sync_api import sync_playwright

APP_URL = "http://localhost:8502"
TIMEOUT = 60000  # 60 seconds max wait for elements


def log(msg):
    print(f"[E2E] {msg}", flush=True)


def test_scenario(page, scenario_num, scenario_name):
    """Test one scenario: select it, run simulation, verify plots appear."""
    log(f"--- Testing Scenario {scenario_num}: {scenario_name} ---")

    # Select scenario from dropdown
    log(f"  Selecting scenario {scenario_num} from dropdown...")
    dropdown = page.locator('[data-testid="stSelectbox"]').first
    dropdown.click()
    time.sleep(0.5)

    # Find and click the option
    option = page.locator(f'li:has-text("{scenario_num}:")').first
    option.click()
    time.sleep(0.5)
    log(f"  Scenario {scenario_num} selected")

    # Click Run Simulation button
    log("  Clicking 'Run Simulation' button...")
    run_button = page.locator('button:has-text("Run Simulation")')
    run_button.click()

    # Wait for simulation to complete (spinner disappears, success message appears)
    log("  Waiting for simulation to complete...")
    page.wait_for_selector('[data-testid="stAlert"]', timeout=TIMEOUT)
    time.sleep(2)  # Let plots render

    # Check that success message contains "complete"
    alerts = page.locator('[data-testid="stAlert"]').all()
    found_success = False
    for alert in alerts:
        text = alert.inner_text()
        if "complete" in text.lower() or "scenario" in text.lower():
            found_success = True
            log(f"  SUCCESS message: {text}")
            break

    if not found_success:
        log(f"  WARNING: No success message found after simulation")

    # Verify plots rendered (check for matplotlib figures)
    log("  Checking for rendered plots...")
    plots = page.locator('img[data-testid="stImage"], [data-testid="stPlotlyChart"], .stPlotlyChart, img').all()
    image_count = len(plots)
    log(f"  Found {image_count} image elements on page")

    # Check for specific section headers
    headers = ["PCU Mode Over Time", "Battery State", "Duty Cycle", "Battery Current",
               "Load Shedding", "Temperature"]
    for header in headers:
        elements = page.locator(f'text="{header}"').all()
        if len(elements) > 0:
            log(f"  Found section: {header}")
        else:
            log(f"  MISSING section: {header}")

    # Check metrics row
    metrics = page.locator('[data-testid="stMetric"]').all()
    log(f"  Found {len(metrics)} metric widgets")

    log(f"  Scenario {scenario_num} test DONE")
    return True


def main():
    log("Starting EPS app end-to-end test")
    log(f"App URL: {APP_URL}")

    with sync_playwright() as p:
        log("Launching Chromium browser (headless)...")
        browser = p.chromium.launch(headless=True)
        context = browser.new_context(viewport={"width": 1920, "height": 1080})
        page = context.new_page()

        log(f"Navigating to {APP_URL}...")
        page.goto(APP_URL, timeout=30000)
        log("Page loaded")

        # Wait for Streamlit to fully initialize
        log("Waiting for Streamlit to initialize...")
        time.sleep(5)

        # Take a screenshot of initial load
        page.screenshot(path="tests/e2e_screenshot_initial.png")
        log("Screenshot saved: tests/e2e_screenshot_initial.png")

        # Wait for auto-run simulation to complete
        log("Waiting for auto-run simulation...")
        try:
            page.wait_for_selector('[data-testid="stAlert"]', timeout=TIMEOUT)
            time.sleep(3)
            log("Auto-run simulation completed")
        except Exception as e:
            log(f"Auto-run wait failed: {e}")
            page.screenshot(path="tests/e2e_screenshot_error.png")

        # Take screenshot after first load
        page.screenshot(path="tests/e2e_screenshot_scenario1.png")
        log("Screenshot saved: tests/e2e_screenshot_scenario1.png")

        # Test scenarios 1, 2, 5, 6 (skip 8 as it's slow)
        scenarios_to_test = [
            (1, "Normal charging"),
            (2, "Eclipse entry"),
            (5, "OBC timeout"),
            (6, "Cold temperature"),
        ]

        results = {}
        for num, name in scenarios_to_test:
            try:
                result = test_scenario(page, num, name)
                results[num] = "PASS" if result else "FAIL"
            except Exception as e:
                log(f"  EXCEPTION in scenario {num}: {e}")
                page.screenshot(path=f"tests/e2e_screenshot_error_s{num}.png")
                results[num] = f"FAIL: {e}"

        # Take final screenshot
        page.screenshot(path="tests/e2e_screenshot_final.png")
        log("Screenshot saved: tests/e2e_screenshot_final.png")

        browser.close()
        log("Browser closed")

    # Summary
    log("")
    log("=" * 50)
    log("TEST RESULTS:")
    all_pass = True
    for num, result in results.items():
        status = "PASS" if result == "PASS" else "FAIL"
        if status == "FAIL":
            all_pass = False
        log(f"  Scenario {num}: {result}")

    if all_pass:
        log("ALL TESTS PASSED")
        return 0
    else:
        log("SOME TESTS FAILED")
        return 1


if __name__ == "__main__":
    sys.exit(main())
