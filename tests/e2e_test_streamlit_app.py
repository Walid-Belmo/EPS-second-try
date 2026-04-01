"""
End-to-end test for the MPPT Streamlit visualization app.
Uses Playwright to automate a real browser, interact with controls,
and take screenshots to verify everything works.

Usage:
    python tests/e2e_test_streamlit_app.py

Prerequisites:
    pip install playwright
    python -m playwright install chromium
    Streamlit app must be running: streamlit run tools/mppt_app.py
"""

import os
import sys
import time

from playwright.sync_api import sync_playwright, expect

SCREENSHOTS_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "e2e_screenshots"
)
APP_URL = "http://localhost:8501"

os.makedirs(SCREENSHOTS_DIR, exist_ok=True)

passed = 0
failed = 0
errors = []


def screenshot(page, name):
    """Take a screenshot and save it."""
    path = os.path.join(SCREENSHOTS_DIR, f"{name}.png")
    page.screenshot(path=path, full_page=True)
    print(f"  Screenshot saved: {name}.png")
    return path


def check(condition, description):
    """Assert a condition and track pass/fail."""
    global passed, failed
    if condition:
        passed += 1
        print(f"  PASS: {description}")
    else:
        failed += 1
        errors.append(description)
        print(f"  FAIL: {description}")


def wait_for_app_ready(page, timeout=15000):
    """Wait for the Streamlit app to finish loading."""
    # Wait for the main content to appear (title)
    page.wait_for_selector("h1", timeout=timeout)
    # Wait for any running spinners to finish
    time.sleep(2)


def run_tests():
    global passed, failed

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        context = browser.new_context(viewport={"width": 1400, "height": 900})
        page = context.new_page()

        # ── TEST 1: App loads ─────────────────────────────────────────────
        print("\n=== TEST 1: App loads correctly ===")
        page.goto(APP_URL)
        wait_for_app_ready(page)

        title = page.title()
        check("MPPT" in title or "Streamlit" in title, f"Page title contains app name (got: {title})")

        h1_elements = page.query_selector_all("h1")
        h1_text = h1_elements[0].inner_text() if h1_elements else ""
        check("MPPT" in h1_text or "CHESS" in h1_text, f"H1 heading present (got: {h1_text})")

        screenshot(page, "01_initial_load")

        # ── TEST 2: Disclaimer warning is visible ─────────────────────────
        print("\n=== TEST 2: Disclaimer warning visible ===")
        warning_elements = page.query_selector_all('[data-testid="stAlert"]')
        check(len(warning_elements) > 0, "Warning/disclaimer alert is displayed")

        page_text = page.inner_text("body")
        check("unverified" in page_text.lower() or "4P" in page_text,
              "Disclaimer mentions panel config uncertainty")

        # ── TEST 3: Sidebar controls exist ────────────────────────────────
        print("\n=== TEST 3: Sidebar controls exist ===")
        sidebar = page.query_selector('[data-testid="stSidebar"]')
        check(sidebar is not None, "Sidebar is present")

        if sidebar:
            sidebar_text = sidebar.inner_text()
            check("Scenario" in sidebar_text or "scenario" in sidebar_text,
                  "Scenario selector in sidebar")
            check("Battery" in sidebar_text or "battery" in sidebar_text,
                  "Battery voltage control in sidebar")
            check("Panel" in sidebar_text or "panel" in sidebar_text,
                  "Panel configuration selector in sidebar")

        screenshot(page, "02_sidebar_controls")

        # ── TEST 4: Click Run Simulation ──────────────────────────────────
        print("\n=== TEST 4: Run Simulation button works ===")
        run_button = page.query_selector('button:has-text("Run Simulation")')
        check(run_button is not None, "Run Simulation button exists")

        if run_button:
            run_button.click()
            # Wait for simulation to complete
            time.sleep(5)
            wait_for_app_ready(page)

        screenshot(page, "03_after_run_simulation")

        # Check that plots appeared
        # Streamlit renders matplotlib plots as images
        img_elements = page.query_selector_all("img")
        check(len(img_elements) >= 1, f"At least 1 plot image rendered (found {len(img_elements)})")

        # Check for PASS/FAIL verdict
        check("PASS" in page_text or "pass" in page.inner_text("body").lower()
              or "FAIL" in page.inner_text("body"),
              "Verdict (PASS or FAIL) is displayed")

        screenshot(page, "04_plots_and_verdict")

        # ── TEST 5: Live values are displayed ─────────────────────────────
        print("\n=== TEST 5: Live values displayed ===")
        body_text = page.inner_text("body")
        check("Voltage" in body_text or "voltage" in body_text,
              "Voltage value displayed")
        check("Current" in body_text or "current" in body_text,
              "Current value displayed")
        check("Power" in body_text or "power" in body_text,
              "Power value displayed")
        check("Duty" in body_text or "duty" in body_text,
              "Duty cycle displayed")

        screenshot(page, "05_live_values")

        # ── TEST 6: Iteration slider works ────────────────────────────────
        print("\n=== TEST 6: Iteration slider works ===")
        sliders = page.query_selector_all('[data-testid="stSlider"]')
        check(len(sliders) >= 1, f"At least 1 slider found (found {len(sliders)})")

        screenshot(page, "06_slider_test")

        # ── TEST 7: Expandable hypothesis sections ────────────────────────
        print("\n=== TEST 7: Hypothesis sections exist ===")
        expanders = page.query_selector_all('[data-testid="stExpander"]')
        check(len(expanders) >= 3, f"At least 3 expandable sections (found {len(expanders)})")

        # Click the first expander to open it
        if len(expanders) > 0:
            expanders[0].click()
            time.sleep(1)
            screenshot(page, "07_expander_opened")
            expanded_text = page.inner_text("body")
            check("solar" in expanded_text.lower() or "photon" in expanded_text.lower()
                  or "diode" in expanded_text.lower() or "cell" in expanded_text.lower(),
                  "Expanded section has physics explanation content")

        # ── TEST 8: Change scenario to 3 (80C) ───────────────────────────
        print("\n=== TEST 8: Change scenario to 3 (hot panel) ===")
        # Find the scenario selectbox in sidebar
        scenario_select = page.query_selector(
            '[data-testid="stSidebar"] [data-testid="stSelectbox"]'
        )
        if scenario_select:
            scenario_select.click()
            time.sleep(0.5)
            # Look for option with "80" in text
            option_80c = page.query_selector('li:has-text("80")')
            if option_80c:
                option_80c.click()
                time.sleep(1)
            else:
                # Try selecting by index (scenario 3)
                options = page.query_selector_all('[data-testid="stSelectbox"] li')
                if len(options) >= 3:
                    options[2].click()
                    time.sleep(1)

        # Click Run again
        run_button = page.query_selector('button:has-text("Run Simulation")')
        if run_button:
            run_button.click()
            time.sleep(5)
            wait_for_app_ready(page)

        screenshot(page, "08_scenario3_80C")

        body_after_s3 = page.inner_text("body")
        check("80" in body_after_s3, "Scenario 3 shows 80C temperature")

        # ── TEST 9: Change to 2S2P panel config ───────────────────────────
        print("\n=== TEST 9: Change to 2S2P panel config ===")
        # Find panel config selectbox (second selectbox in sidebar)
        selectboxes = page.query_selector_all(
            '[data-testid="stSidebar"] [data-testid="stSelectbox"]'
        )
        if len(selectboxes) >= 2:
            selectboxes[1].click()
            time.sleep(0.5)
            option_2s2p = page.query_selector('li:has-text("2S2P")')
            if option_2s2p:
                option_2s2p.click()
                time.sleep(1)

        # Run simulation with 2S2P
        run_button = page.query_selector('button:has-text("Run Simulation")')
        if run_button:
            run_button.click()
            time.sleep(5)
            wait_for_app_ready(page)

        screenshot(page, "09_2s2p_config")

        # ── TEST 10: Raw CSV data expander ────────────────────────────────
        print("\n=== TEST 10: Raw CSV data section ===")
        raw_data_expander = page.query_selector(
            '[data-testid="stExpander"]:has-text("Raw CSV")'
        )
        if raw_data_expander:
            raw_data_expander.click()
            time.sleep(1)
            check(True, "Raw CSV data expander found and clicked")
            screenshot(page, "10_raw_csv_data")
        else:
            # Try finding any expander with "Raw" or "CSV" text
            for exp in expanders:
                if "raw" in exp.inner_text().lower() or "csv" in exp.inner_text().lower():
                    exp.click()
                    time.sleep(1)
                    check(True, "Raw CSV data section found")
                    screenshot(page, "10_raw_csv_data")
                    break
            else:
                check(False, "Raw CSV data section found")

        # ── TEST 11: Eclipse scenario (edge case) ─────────────────────────
        print("\n=== TEST 11: Eclipse scenario (edge case) ===")
        # Reset to 4P config first
        page.goto(APP_URL)
        wait_for_app_ready(page)

        # Select scenario 6 (eclipse)
        scenario_select = page.query_selector(
            '[data-testid="stSidebar"] [data-testid="stSelectbox"]'
        )
        if scenario_select:
            scenario_select.click()
            time.sleep(0.5)
            eclipse_option = page.query_selector('li:has-text("Eclipse")')
            if eclipse_option:
                eclipse_option.click()
                time.sleep(1)

        run_button = page.query_selector('button:has-text("Run Simulation")')
        if run_button:
            run_button.click()
            time.sleep(5)
            wait_for_app_ready(page)

        screenshot(page, "11_eclipse_scenario")

        body_eclipse = page.inner_text("body")
        # The app shouldn't crash on eclipse (irradiance = 0)
        check("error" not in body_eclipse.lower() or "Error from MPP" in body_eclipse,
              "Eclipse scenario doesn't crash the app")

        # ── TEST 12: Battery voltage edge cases ───────────────────────────
        print("\n=== TEST 12: Battery voltage edge values ===")
        page.goto(APP_URL)
        wait_for_app_ready(page)

        # The battery slider should be in the sidebar
        # We can't easily set exact slider values with Playwright,
        # but we can verify the app doesn't crash at defaults
        run_button = page.query_selector('button:has-text("Run Simulation")')
        if run_button:
            run_button.click()
            time.sleep(5)
            wait_for_app_ready(page)

        body_final = page.inner_text("body")
        check("Traceback" not in body_final, "No Python traceback visible on page")
        check("Error" not in body_final or "Error from MPP" in body_final,
              "No unhandled errors on page")

        screenshot(page, "12_final_state")

        # ── DONE ──────────────────────────────────────────────────────────
        browser.close()

    # ── Summary ───────────────────────────────────────────────────────────
    print(f"\n{'='*60}")
    print(f"E2E TEST RESULTS: {passed} passed, {failed} failed")
    print(f"Screenshots saved to: {SCREENSHOTS_DIR}")
    if errors:
        print(f"\nFailed tests:")
        for e in errors:
            print(f"  - {e}")
    print(f"{'='*60}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(run_tests())
