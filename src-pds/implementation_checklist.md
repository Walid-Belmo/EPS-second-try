# Source PDS Implementation Checklist

Use this checklist before stopping for review at each layer.

1. A person who barely knows the project can read the function names and understand the story.
2. Each function name says what the function does, not just what area of the project it belongs to.
3. Each comment explains why the line or function is necessary for the EPS requirements.
4. The comment answers: "Why is this here, and why is this the right thing to do here?"
5. Hardware-changing code is separated from decision-making code.
6. The code uses the simplest structure that still makes the behavior explicit.
7. The new layer stays coherent with the working code in `src/`.
8. Stop after one abstraction layer so the user can read and correct it before going deeper.
