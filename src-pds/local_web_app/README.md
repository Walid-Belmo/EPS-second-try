# Source PDS — Local Web App

Single Python process serving every Source PDS demo page. Owns the ESP32
USB serial link, routes each page by URL path, prefixes per-page endpoints
under `/api/<page>/...` so they never collide.

## Run

```bash
python src-pds/local_web_app/run_local_web_app.py
```

Open `http://127.0.0.1:8000`. The landing page links to:

- `/mppt` — MPPT convergence demo
- `/state` — state transition demo

If the COM port differs from the default (`COM3`):

```bash
python src-pds/local_web_app/run_local_web_app.py --serial-port COM7
```

## Architecture

```
src-pds/local_web_app/
├── run_local_web_app.py              HTTP routing, lifecycle
├── serial_bridge_shared_by_pages.py  Single SerialBridge + PageState + parser
├── mppt_convergence_page_actions.py  MPPT POST handlers and command builders
├── state_transition_page_actions.py  State scenarios and orchestration
└── static/
    ├── index.html                    Landing page (two cards)
    ├── mppt/                         MPPT page HTML/CSS/JS
    └── state/                        State page HTML/CSS/JS
```

Routing table:

| Method | Path | Module |
|---|---|---|
| GET | `/` | landing page |
| GET | `/mppt` | MPPT HTML |
| GET | `/state` | State HTML |
| GET | `/static/mppt/<file>` | MPPT page static asset |
| GET | `/static/state/<file>` | State page static asset |
| GET | `/api/status` | shared snapshot |
| POST | `/api/connect` | shared (open serial) |
| POST | `/api/off` | shared (off + clear) |
| POST | `/api/get_values` | shared debug poke |
| POST | `/api/mppt/start_mppt` | MPPT module |
| GET | `/api/state/scenarios` | State module |
| POST | `/api/state/enter_state_test_mode` | State module |
| POST | `/api/state/run_scenario` | State module |
| POST | `/api/state/run_all_scenarios` | State module |

## Off-before-switch UX gate

Each page disables its "Start" button (MPPT: Start MPPT Test; State: Enter
State Test Mode) until **Off** is clicked at least once after page load.
A yellow banner under the header reminds the user. The gate is purely
front-end — the backend accepts requests in any order.

## Adding a new page

1. Drop HTML/CSS/JS under `static/<new-page>/`.
2. Add the handlers + command builders to a new
   `<new_page>_page_actions.py`.
3. Import it in `run_local_web_app.py` and add the routes.

No new COM port. No new process.

## Related docs

- `src-pds/state-transitions.md` — target FSM spec and open issues.
- `src-pds/plan.md` — overall demo-page roadmap.
