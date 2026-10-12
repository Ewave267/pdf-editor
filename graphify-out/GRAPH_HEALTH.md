[graphify] MultiDiGraph edge-collapse diagnostic
input: <in-memory>
input_stage: provided JSON (normal graph.json is post-build)
effective_directed: <direct-call>
nodes: 1085
unverified_code_nodes: 0
raw_edges: 2416
valid_candidate_edges: 1973
missing_endpoint_edges: 0
dangling_endpoint_edges: 419
external_reference_edges: 24
self_loop_edges: 0
exact_duplicate_edges: 0
directed_unique_endpoint_pairs: 1914
directed_same_endpoint_collapsed_edges: 59
undirected_unique_endpoint_pairs: 1906
undirected_same_endpoint_collapsed_edges: 67
same_endpoint_group_count: 57
relation_variant_groups: 44
source_file_variant_groups: 0
source_location_variant_groups: 5
context_variant_groups: 14
post_build_graph_type: Graph
post_build_edges: 2347
producer_suppression_sites: 12
producer_suppression_examples:
  - L1764 seen_ids arity=unknown
  - L2328 seen_ids arity=unknown
  - L2330 seen_doc_refs arity=unknown
  - L2808 seen_ids arity=unknown
  - L2955 seen_ids arity=unknown
  - L3807 seen_keys arity=unknown
  - L3980 seen_keys arity=unknown
  - L6385 seen_ids arity=unknown
examples:
  - src_pdf_pdfdocument_pdfdocument -> src_pdf_pdfdocument_request edges=3 relations=['contains', 'references'] locations=['L143', 'L194', 'L195'] contexts=['', 'field', 'generic_arg']
  - src_pdf_pdfrenderworker_exportpages -> src_pdf_pdfrenderworker_cpp_qbytearray edges=3 relations=['references'] locations=['L49'] contexts=['generic_arg', 'parameter_type', 'return_type']
  - src_app_windows_sandbox_main_quote -> wstring edges=2 relations=['references'] locations=['L59'] contexts=['parameter_type', 'return_type']
  - src_app_windows_sandbox_main_privateruntime -> src_app_windows_sandbox_main_privateruntime_path edges=2 relations=['defines', 'references'] locations=['L113'] contexts=['field']
  - src_content_addedcontent_addedcontent_bounded -> src_content_addedcontent_cpp_qrectf edges=2 relations=['references'] locations=['L362'] contexts=['parameter_type', 'return_type']
note: normal graph.json is post-build; raw producer loss must be measured earlier.