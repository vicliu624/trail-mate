# LVGL UI Best Practices

### Writing UI Code That Is Readable, Maintainable, and Pleasant to Work With

LVGL is powerful, but its **imperative API and verbose style system** make UI code hard to read and easy to turn into noise.

This document describes a **proven structure and coding style** for LVGL projects that makes UI code:

* Easy to understand at a glance
* Easy to modify without side effects
* Scalable as the number of pages grows

The goal is simple:

> **UI code should read like a layout diagram, not like a dump of API calls.**

---

## 0. Decide the Reading Order First

Before writing any code, decide how UI code is meant to be read.

**Recommended reading order:**

1. **Layout** – object tree and spatial relationships
2. **Styles** – visual appearance (colors, borders, typography)
3. **Components** – business logic and refresh flow
4. **Input** – rotary / keyboard / touch navigation

If the code does not follow this order, it will be hard to reason about.

---

## 1. Every Page Must Start with a Wireframe

Each `*_layout.cpp` file should begin with an **ASCII wireframe and tree view**.

This wireframe answers, instantly:

* How many columns or rows exist?
* Which containers grow or are fixed?
* Where pagination or action bars live?

Example intent (simplified):

```
Root (ROW)
├─ FilterPanel (80px)
├─ ListPanel (grow=1)
│  ├─ ListContainer (grow=1)
│  └─ BottomBar (ROW)
└─ ActionPanel (100px)
```

### Rules

* The wireframe describes **structure only**
* Do **not** describe colors, borders, or fonts
* Treat it as architectural documentation, not decoration

---

## 2. Split UI Code into Four Layers

### 2.1 Layout Layer – Structure Only

**Purpose:**
Define the object tree and layout constraints.

**Allowed in layout:**

* `lv_obj_create`, `lv_btn_create`, `lv_label_create`
* `lv_obj_set_size`
* `lv_obj_set_flex_flow`, `lv_obj_set_flex_grow`, `lv_obj_set_flex_align`
* `lv_obj_align`, `lv_obj_center`
* Scroll disabling

**Forbidden in layout:**

* `lv_obj_set_style_*`
* `lv_obj_add_event_cb`
* Business logic or data formatting

Layout functions should feel like **blueprints**, not UI logic.

---

### 2.2 Style Layer – Visual Rules Only

**Purpose:**
Centralize all visual decisions.

**Rules:**

* Define styles once (`init_once()` pattern)
* Expose semantic helpers like:

  * `apply_panel_main()`
  * `apply_btn_filter()`
  * `apply_list_item()`
* Use LVGL states (`LV_STATE_CHECKED`, `LV_STATE_DISABLED`, `LV_STATE_FOCUSED`)

**Forbidden in styles:**

* Creating objects
* Business logic
* Layout decisions

> Logic sets states. Styles react to states.

---

### 2.3 Component Layer – UI Logic and Refresh Flow

**Purpose:**
Decide *what* is shown and *when* it updates.

Responsibilities:

* `refresh_ui()` flow
* Pagination logic
* Enabling/disabling buttons
* Formatting display text
* Event handlers for UI actions
* Modal lifecycle management

**Rules:**

* Never write raw style code here
* Never change layout structure here
* Use `style::apply_*()` and `layout::*()` only

A good `refresh_ui()` reads like a checklist, not a drawing routine.

---

### 2.4 Input Layer – Navigation and Focus

**Purpose:**
Handle rotary encoders, keys, and focus flow.

Responsibilities:

* Column focus switching
* List navigation
* Press/confirm behavior

**Rules:**

* Input modifies **state**, not appearance
* UI updates happen through `refresh_ui()` or state transitions
* No direct color or style manipulation

This keeps interaction logic independent of visuals.

---

## 3. Eliminate “Style Noise”

### Rule 1: Direct `set_style_*` Is Technical Debt

If you write style code inline, you will copy it everywhere.

Centralize styles or you will lose consistency.

---

### Rule 2: Use LVGL States, Not Colors

Never encode meaning with colors in logic code.

Instead:

* Selected → `LV_STATE_CHECKED`
* Disabled → `LV_STATE_DISABLED`
* Focused → `LV_STATE_FOCUSED`

---

### Rule 3: `refresh_ui()` Must Contain Zero Visual Detail

If you see colors, borders, or font sizes in `refresh_ui()`, the architecture is broken.

---

## 4. Use Consistent Naming and Object Lifetime Rules

### Naming conventions

* Panels: `filter_panel`, `list_panel`, `action_panel`
* Containers: `sub_container`, `bottom_container`
* Buttons: `*_btn`
* Labels: `*_label`

### Lifetime clarity

* Persistent objects: panels, containers, pagination buttons
* Rebuilt objects: list items

Make object lifetime explicit and predictable.

---

## 5. Make Refresh Strategy Explicit

Document whether UI elements are:

* Rebuilt every refresh
* Updated incrementally
* Created once and reused

This prevents accidental performance regressions later.

---

## 6. Separate Layout, Style, and Logic Constants

* Layout constants → layout files
* Style constants → style files
* Business constants → component files

Never mix them.

---

## 7. Optional: Small Structural Helpers (Not Frameworks)

Helpers like:

* `create_column_panel()`
* `create_text_button()`

Are fine **if** they stay structural and style-free.

Avoid building a mini framework that hides LVGL.

---

## 8. Final Checklist (Recommended for Repos)

Every UI page should satisfy:

* [ ] Layout file contains wireframe + tree view
* [ ] Layout code has zero style calls
* [ ] Styles are centralized and state-driven
* [ ] Components contain no visual details
* [ ] Input logic only mutates state
* [ ] Constants are correctly layered