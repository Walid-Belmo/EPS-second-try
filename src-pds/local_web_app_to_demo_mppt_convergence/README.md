# MPPT Convergence Web App

This folder contains the local web page used to demonstrate MPPT convergence
with the Source PDS firmware and the Source PDS ESP32 bridge.

Run it from the repository root:

```text
python src-pds/local_web_app_to_demo_mppt_convergence/run_mppt_convergence_page.py
```

Then open:

```text
http://127.0.0.1:8001
```

The page sends these text commands to the ESP32 bridge:

```text
off
start_mppt_demo curve=quadratic a=-10 b=150 c=720 v_min=0 v_max=18000 battery_voltage=7400
stream_values on period=250 fields=all
get_values fields=all
```
