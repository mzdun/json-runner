set(GRAPHVIZ_GRAPH_HEADER "graph [\n  fontname = \"Montserrat SemiBold\";\n];\nnode [\n  fontsize = \"12\";\n  fontname = \"Montserrat\";\n];\nedge [\n  fontname = \"Montserrat Light\";\n];")
set(GRAPHVIZ_GENERATE_PER_TARGET FALSE)
set(GRAPHVIZ_GENERATE_DEPENDERS FALSE)
set(GRAPHVIZ_IGNORE_TARGETS "CONAN_LIB::.*"
    "GTest::.*"
    ".*_DEPS_TARGET"
    ".*-test"
    "mock-.*"
)
