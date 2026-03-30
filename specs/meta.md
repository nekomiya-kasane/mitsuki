# UI/UX Meta-Architecture

---

## 1. Interaction Grammar: Noun-Verb vs. Verb-Noun

This is the most fundamental interaction paradigm, determining user cognitive flow and the system's command dispatch architecture.

| Dimension             | Noun-Verb (Select Geometry, then Tool)                                     | Verb-Noun (Select Tool, then Geometry)                                        |
| :-------------------- | :------------------------------------------------------------------------- | :---------------------------------------------------------------------------- |
| **Cognitive Model**   | **Object-Focused**: "What am I operating on?" Aligns with human intuition. | **Task-Focused**: "What do I want to do?" Aligns with procedural engineering. |
| **UI Manifestation**  | Context Menus, Hover Toolbars, Pie Menus.                                  | Global Ribbons, Command Panels.                                               |
| **Efficiency**        | **Very High** (complies with Fitts's Law, minimal mouse travel).           | **Low** (frequent back-and-forth between canvas and ribbon).                  |
| **Discoverability**   | Poor: Users may not know what to select to reveal a tool.                  | Excellent: Tools are exposed globally, good for beginners.                    |
| **Complex Workflows** | Weak: Hard to manage multi-step, multi-input features.                     | **Strong**: Ideal for complex feature wizards (e.g., Loft, Sweep).            |

**Personal Recommandation: Hybrid Grammar (Noun-Verb Dominant)**
Modern CAD relies on a hybrid approach. It prioritizes Noun-Verb for extreme immersion and contextual recommendation (e.g., selecting an edge immediately proposes Fillet/Chamfer and spawns an in-place slider). However, for complex topological operations (like Lofting), it gracefully degrades to a Verb-Noun wizard mode.

## 2. Locus of Attention

This addresses the "Eye-Ping-Pong" fatigue caused by traditional CAD interfaces where users constantly dart their eyes between the 3D model and peripheral panels.

| Design Choice             | Description                                                                              | Best For                                                    | Architecture Requirement (miki)                                                                       |
| :------------------------ | :--------------------------------------------------------------------------------------- | :---------------------------------------------------------- | :---------------------------------------------------------------------------------------------------- |
| **In-Canvas (Immersive)** | Rendering controls directly at the mouse cursor (Gizmos, 3D drag handles, radial menus). | Continuous adjustments, conceptual design, rapid iteration. | Requires a robust `ViewportWidgetLayer` with depth-testing, SDF fonts, and screen-space interactions. |
| **Out-of-Canvas**         | Side property panels, bottom status bars, top ribbons.                                   | Tedious data entry, complex parameter tables, GD&T configs. | Relies on the host UI framework (Qt/Slint) via `IUiBridge`.                                           |

**Personal Recommandation: Maximize In-Canvas Operations**
Push as many interactions as possible into the 3D viewport. A fillet radius should not be typed into a left-side panel; it should be manipulated via an SDF-rendered virtual slider (Viewport Slider) directly over the geometry. Only non-geometric metadata (e.g., material physics, strict tolerance tables) should retreat to out-of-canvas panels.

## 3. State Management

Should the system have explicit "state locks" (e.g., entering Sketch Mode, Assembly Mode)?

- **Strict Modal**: (e.g., SolidWorks Sketch mode). Pros: Pure environment, fewer misclicks, keyboard shortcuts can be contextually overloaded. Cons: "Mode Isolation" — users forget their current mode and cannot cross-reference easily (e.g., measuring a part while in a sketch).
- **Modeless**: (e.g., direct modelers). 3D and 2D coexist simultaneously. Pros: Extremely fluid. Cons: Prone to accidental topological changes.

**Personal Recommandation: Soft-Modal via Visual Metaphors**
Break down hard modal walls. Instead of locking the UI, use visual metaphors. When entering a sketch, do not lock 3D camera rotation. Instead, fade non-sketch geometry to 30% opacity (`LayerStack` opacity override) while retaining full snapping and measurement capabilities against the ghosted 3D bodies.

## 4. Intent Capture

How do users express mathematical constraints to the solver?

- **Explicit Constraints**: Users manually click "Coincident", "Concentric", or "Parallel" icons. (Rigorous, traditional).
- **Heuristic Inferencing**: The system predicts intent based on spatial proximity and drag trajectories. (Fast, modern).

**Personal Recommandation: AI-Assisted Predictive UI**
Maintain a strict Directed Acyclic Graph (DAG) constraint solver under the hood, but wrap it in a predictive UI. As users drag entities, the system infers constraints and renders **Ghosting / Preview Layers** instantly. If the user pauses or confirms, it commits as an explicit constraint. This necessitates an ultra-low-latency **Preview Rendering Pipeline** (as designed in miki Phase 8) that does not pollute the main undo/redo stack until committed.

## 5. Topological Mutability

This dictates the user's mental model of how the geometry is constructed.

- **History-Based (Feature Tree)**: Users manage a timeline of operations (Extrude -> Fillet -> Shell). UI focuses on rollback bars and parameter editing.
- **Direct Modeling**: Users interact only with the final Boundary Representation (B-Rep), pushing and pulling faces directly.

**Personal Recommandation: Implicitly Parameterized Direct Modeling**
Provide the tactile illusion of Direct Modeling (push/pull faces) in the UI, but under the hood, record these interactions as parameterized operations in the `OpHistory` and `CommandBus`. When a user selects a face, the UI should optionally trace back and highlight the historic node that generated it, unifying WYSIWYG interaction with rigorous auditability.

## 6. Design Language & Visual Density

CAD is a heavy-duty productivity tool, creating friction between expert needs (high density) and modern UI aesthetics (minimalism).

- **High Density (Traditional)**: Screens packed with 16x16 icons, dense trees. High cognitive load but minimal mouse travel.
- **Low Density (Modern Web/Mobile)**: Large whitespace, huge touch-friendly targets. Often disastrous for CAD professionals.

**Personal Recommandation: Progressive Disclosure & Semantic Coloring**

1.  **Resting State Minimalism**: The 3D canvas should consume >90% of the screen. Peripherals are slim and unobtrusive.
2.  **Interaction State High-Density**: Upon selection, a high-density, context-aware menu (Drill-down/Pie menu) explodes instantly around the cursor.
3.  **Semantic Geometry Coloring**: The UI chrome itself should remain neutral (dark/gray). Status and errors should be communicated through the 3D geometry's color (e.g., Green = Fully constrained sketch, Blue = Under-constrained, Red = Topological interference).

## 7. Spatial Computing & AR/VR Readiness

With the mainstream adoption of spatial computing (XR headsets), CAD UI must be decoupled from flat 2D monitors.

**Personal Recommandation: Volumetric & Multi-Modal Ready**

- **Depth-Aware UI**: Viewport widgets (dimensions, tooltips) must be bill-boarded but depth-tested correctly against the Z-buffer to avoid breaking immersion.
- **Text as a First-Class Citizen**: Relying on OS text rendering is obsolete. miki's heavy investment in MSDF (Multi-channel Signed Distance Fields) and GPU Direct Curve Text ensures that PMI (Product and Manufacturing Information) and in-canvas typography remain flawlessly anti-aliased at any arbitrary zoom level or VR focal plane.
- **Event Abstraction**: The `IUiBridge` must handle generalized `Ray` intersections and 6-DOF input events, not just 2D mouse coordinates.
