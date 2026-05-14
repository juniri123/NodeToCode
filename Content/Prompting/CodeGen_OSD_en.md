Convert the Blueprint into Unreal C++ code based on the files below.
The final output must not be a "Blueprint dump"; it must be actual Unreal gameplay C++ code that a human can maintain.
Keep the Blueprint tracing information in comments, and prioritize readability in the actual code.

File roles:
- *_flow.txt: Execution flow (exec flow) information
- *_graph.txt or pasted text: Detailed node information (data pin connections)
- *_structs.txt: Struct types/fields used in the Blueprint

Important rules:
1. The execution flow must be constructed based on flow.txt.
2. graph.txt must be used only to restore data pins, variables, and function input values.
3. Reconstruct the code reasonably based on the Blueprint/graph/structs information and Unreal Engine context.
   However, if any inference or assumption is made without direct evidence in the input data, it must be explicitly marked in a comment or TODO.
   Examples:
   - TODO: Exact type needs review because it is not present in structs.txt
   - Assumed from UE convention
   - Inferred from Blueprint pin usage
   - Estimated signature based on graph connection
4. Blueprint node names (K2Node_...) must be kept only in comments ([Flow.txt], [Flow], [NodeInfo]) and mapping descriptions.
   Do not use BP intermediate names such as K2Node_*, *_ReturnValue, or PromotableOperator_* for variable names, return value names, or temporary value names in the actual C++ code.
5. Merge points may be reorganized into human-readable C++ flow.
6. Write in Unreal Engine-style C++.
7. Preserve gameplay-meaningful function names and variable names that explicitly exist in the Blueprint as much as possible.
   However, do not preserve Blueprint intermediate names (K2Node_*, Temp*, *_ReturnValue, etc.).
8. Generate return value names that reflect the context.
   Every function call result, operation result, Map_Find result, and bool result must use a semantic name that reveals gameplay meaning.
   Examples:
   - K2Node_CallFunction_3_ReturnValue ❌ → InstigatorOwner ⭕
   - K2Node_CallFunction_1_ReturnValue ❌ → StatsComponent ⭕
   - K2Node_CallFunction_30_ReturnValue ❌ → FreshnessType ⭕
   - K2Node_CallFunction_13_ReturnValue ❌ → FreshnessRule ⭕
   - K2Node_PromotableOperator_1_ReturnValue ❌ → FoodPoisoningAmount ⭕

Comment format rules:
The structure below must be preserved.

Example:

// =====================================================
// [Flow.txt]
// ➡️ 📌True::P66 from (📋K2Node_IfThenElse_4::N16)
// │   📋K2Node_GenericCreateObject_0::N23
// │   📌exec::P81 from (📋K2Node_GenericCreateObject_0::N23) → 📋K2Node_CallFunction_10::N6
//
// [Flow]
// IfThenElse_4 True
//  -> GenericCreateObject_0
//  -> CallFunction_10 : GetFilteredGameObjects
//  -> MacroInstance_0 : ForEachLoop
//
// NodeInfo:
// GenericCreateObject Class = InventoryFilter
// GenericCreateObject ReturnValue -> GetFilteredGameObjects Target
// Self -> Inventory
// GetFilteredGameObjects.GameObjects -> ForEachLoop Array
// =====================================================

Explanation:
- [Flow.txt]
  -> Write the original flow.txt text as-is as much as possible.
  -> Do not summarize or translate it.

- [Flow]
  -> Summarize the execution flow in a human-readable way.

- [NodeInfo]
  -> Restore the data pin connections from graph.txt and write them in an organized way.
  -> Organize data pin flow in a human-readable "A -> B(Target)" form.

Additional rules:
- A [Flow.txt] block must not cover an entire function at once.
- A [Flow.txt] block must correspond 1:1 to the minimum execution section of the C++ code immediately below it.
- Comment blocks for parent control statements must not include detailed Flow.txt for child branches/bodies.
- When an if/switch/loop appears, attach only the Flow.txt section that evaluates or enters that control statement directly above it.
- Inside an if/switch/loop, attach a separate block for the Flow.txt section corresponding to that branch/body.
- Preserve common merge points in comments as well.
- Mark return points based on the FunctionResult node.
- Preserve data pin flow, but do not create unnecessary Blueprint intermediate temp variables.
- Organize the implementation into human-readable Unreal C++ flow.

- General type inference based on Unreal Engine conventions (for example, AActor, UActorComponent, TMap) is allowed without a separate TODO.
- Gameplay-specific type/signature inference must be marked with a TODO or an Assumed/Inferred comment.
- Distinguish confirmed information from inferred information.

- Keep K2Node names only in comments ([Flow.txt], [Flow], [NodeInfo]) so the BP source remains grep-able.
- Preserve BP node IDs only in code comments for grep/debugging.
- In the actual C++ code, do not use Blueprint intermediate names such as K2Node_*, *_ReturnValue, or PromotableOperator_* as variable names.
- Use human-maintainable Unreal C++ style variable names in the actual C++ code.
- Function return values, temporary values, and operation results must use semantic names based on context.
- If a name cannot be determined, do not use the K2Node name; use a TODO semantic name instead.
  Example: UnknownTargetObject /* TODO: source K2Node_CallFunction_23 */

Output format:
1. Function signature
2. C++ code with detailed comments
3. TODO / items to verify list at the end
