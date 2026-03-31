Coding Conventions
These conventions apply across all languages used in this project.
The single goal is to make the code faster to debug — not to be pure, not to be impressive.
---
The priority order — read this first
1. Make the feature work.
2. If possible, make it work following these conventions.
3. If the conventions would risk breaking the feature or slow down implementation, ignore them entirely.
If you find code online that solves the problem, use it as-is.
Technical debt is acceptable. A feature that does not work is not.
Add a short comment explaining what the code does and move on.
---
1. Naming — be absurdly explicit
Every variable and function name must read as a plain English sentence or phrase.
First word lowercase, every subsequent word starts with a capital letter,
words separated by underscores:
```
the_Single_Variable_That_Holds_The_Current_State
build_All_Settings_For_The_Claude_Session
the_List_Of_Lines_Received_So_Far
calculate_The_Grand_Total_For_The_Whole_Cart
```
Variables describe what the data IS:
```
the_Api_Key_Loaded_From_Environment_Variables
the_List_Of_Lines_Received_So_Far
the_Number_Of_Lines_Received_So_Far
the_Running_Claude_Process
```
Functions describe what they DO and what they return:
```
build_All_Settings_For_The_Claude_Session
convert_All_Settings_Into_A_List_Of_Cli_Arguments
produce_A_New_State_By_Adding_One_Line_To_The_Existing_State
calculate_The_Grand_Total_For_The_Whole_Cart
```
Never abbreviate. Never use single letters. Never use vague words like
`data`, `info`, `temp`, `obj`, `val`, `res`, `cb`, `fn`, `x`, `i`.
This naming rule applies even when you are not following FP.
Even if a piece of code is copied from Stack Overflow and not refactored,
rename the variables to follow this convention so you can read it later.
---
2. Functional Programming — when you can
Write every piece of logic as a pure function when possible. A pure function:
Takes its inputs as arguments
Returns a new value
Never modifies anything that was passed to it
Never reads from or writes to anything outside of itself
Never mutate if you can avoid it:
Python:
```python
# WRONG
the_List_Of_Lines.append(the_New_Line)

# CORRECT
the_Updated_List_Of_Lines = [*the_List_Of_Lines, the_New_Line]
```
TypeScript:
```typescript
// WRONG
the_List_Of_Lines.push(the_New_Line)

// CORRECT
const the_Updated_List_Of_Lines = [...the_List_Of_Lines, the_New_Line]
```
Local variables inside functions are completely fine — they exist only
for the duration of that one call and have no memory between calls.
Functions inside functions are completely fine — this is called a closure.
Use it when you want to bake a value into a function at creation time:
```python
def build_The_Price_Calculator_For_A_Specific_Tax_Rate(the_Tax_Rate):

    def calculate_The_Final_Price_Including_Tax(the_Price_Before_Tax):
        return the_Price_Before_Tax * (1 + the_Tax_Rate)

    return calculate_The_Final_Price_Including_Tax
```
---
3. Data — plain objects, avoid classes
Prefer plain objects or dictionaries for holding data.
Classes are allowed when an external library requires them or when
there is a strong reason — but never use them just to group data together.
Python — use a dictionary or SimpleNamespace:
```python
# Dictionary
the_Item = {
    "the_Name_Of_The_Product": "mouse",
    "the_Price_In_Euros": 40,
    "the_Quantity_The_Customer_Ordered": 2
}
the_Item["the_Price_In_Euros"]

# SimpleNamespace if you prefer dot access
from types import SimpleNamespace
the_Item = SimpleNamespace(
    the_Name_Of_The_Product = "mouse",
    the_Price_In_Euros = 40,
    the_Quantity_The_Customer_Ordered = 2
)
the_Item.the_Price_In_Euros
```
TypeScript:
```typescript
const the_Item = {
    the_Name_Of_The_Product: "mouse",
    the_Price_In_Euros: 40,
    the_Quantity_The_Customer_Ordered: 2
}
the_Item.the_Price_In_Euros
```
Updating without mutating:
```python
# Python
the_Discounted_Item = {
    **the_Item,
    "the_Price_In_Euros": the_Item["the_Price_In_Euros"] * 0.9
}
```
```typescript
// TypeScript
const the_Discounted_Item = {
    ...the_Item,
    the_Price_In_Euros: the_Item.the_Price_In_Euros * 0.9
}
```
---
4. Side effects — isolate them at the edges
Label pure functions and side effects clearly in comments so you
always know which parts of the code touch the outside world:
```python
# --- PURE FUNCTIONS ---

def build_The_Config(the_Model):
    return { "the_Model": the_Model }

def convert_The_Config_Into_Cli_Arguments(the_Config):
    return ["--model", the_Config["the_Model"]]


# --- SIDE EFFECTS ---

def actually_Launch_The_Process_On_The_Terminal(the_Arguments):
    return subprocess.Popen(["claude"] + the_Arguments, stdout=subprocess.PIPE)
```
---
5. State — use reducers
Never mutate a variable in place. Replace it entirely each time:
```python
def create_The_Initial_Empty_Output_State():
    return {
        "the_Lines_Received_So_Far": [],
        "the_Total_Number_Of_Lines_Received": 0
    }

def produce_A_New_State_By_Adding_One_Line_To_The_Existing_State(
    the_Current_State,
    the_New_Line_That_Just_Arrived
):
    return {
        "the_Lines_Received_So_Far": [
            *the_Current_State["the_Lines_Received_So_Far"],
            the_New_Line_That_Just_Arrived
        ],
        "the_Total_Number_Of_Lines_Received": (
            the_Current_State["the_Total_Number_Of_Lines_Received"] + 1
        )
    }

the_Single_Variable_That_Holds_The_Current_State = create_The_Initial_Empty_output_State()

the_Single_Variable_That_Holds_The_Current_State = (
    produce_A_New_State_By_Adding_One_Line_To_The_Existing_State(
        the_Single_Variable_That_Holds_The_Current_State,
        the_New_Line_That_Just_Arrived
    )
)
```
---
6. Structure — pipeline first, details below
The top level function reads like a recipe. Details live in the functions it calls:
```python
def run_The_Whole_Application():
    the_Settings  = build_All_Settings_For_The_Claude_Session("claude-opus-4-5")
    the_Arguments = convert_All_Settings_Into_A_List_Of_Cli_Arguments(the_Settings)
    the_Process   = actually_Launch_The_Process_On_The_Terminal(the_Arguments)
    start_Listening_To_The_Output_And_Accumulate_It(the_Process)

run_The_Whole_Application()
```
---
7. Comments — explain WHY, not WHAT
```python
# WRONG — the name already says this
# builds the browser tool config
def build_The_Settings_For_The_Browser_Tool():
    ...

# CORRECT — explains a non-obvious decision
# headless is forced here because CI environments have no display server
def build_The_Settings_For_The_Browser_Tool():
    ...
```
---
8. When you are not following these conventions
Add one comment line so you know why that block looks different
when you come back to debug it later:
```python
# NOTE: copied from Playwright docs — not refactored
browser = playwright.chromium.launch()
page = browser.new_page()
```
---
9. File and folder structure
These principles apply regardless of language or type of app:
One folder for all source code, one folder for all documentation
One file per topic — never mix two unrelated things in the same file
File names follow the same naming convention as variables and functions:
`the_Browser_Tool.py` not `browser.py`
`the_Known_Issues.md` not `issues.md`
The entry point of the app always lives at the root of the source folder
and is always called `main`
Read `current_plan.md` before starting any session
Update `current_plan.md` before ending any session
---
Summary
Rule	Priority
Make the feature work	Always — this comes first
Absurdly explicit names	Always — even in non-FP code
Pure functions and no mutation	When possible
Plain objects over classes	When possible
Side effects at the edges	When possible
Reducers for state	When possible
Comment when deviating	Always — one line is enough
