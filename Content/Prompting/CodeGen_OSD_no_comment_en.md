Convert the Blueprint into Unreal C++ code based on the files below.
The final output must not be a "Blueprint dump"; it must be actual Unreal gameplay C++ code that a human can maintain.
Prioritize readability in the actual code, and faithfully implement the flow in *_flow.txt.

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
4. Do not use BP intermediate names such as K2Node_*, *_ReturnValue, or PromotableOperator_* for variable names, return value names, or temporary value names in the actual C++ code.
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


Additional rules:
- Preserve data pin flow, but do not create unnecessary Blueprint intermediate temp variables.
- Organize the implementation into human-readable Unreal C++ flow.

- General type inference based on Unreal Engine conventions (for example, AActor, UActorComponent, TMap) is allowed without a separate TODO.
- Gameplay-specific type/signature inference must be marked with a TODO or an Assumed/Inferred comment.
- Distinguish confirmed information from inferred information.

- In the actual C++ code, do not use Blueprint intermediate names such as K2Node_*, *_ReturnValue, or PromotableOperator_* as variable names.
- Use human-maintainable Unreal C++ style variable names in the actual C++ code.
- Function return values, temporary values, and operation results must use semantic names based on context.
- If a name cannot be determined, do not use the K2Node name; use a TODO semantic name instead.
  Example: UnknownTargetObject /* TODO: source K2Node_CallFunction_23 */

Output format:
1. Function signature
2. C++ code
3. TODO / items to verify list at the end
