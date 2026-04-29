#[modes]
// ....

#[specializations]
// Empty for now, but the tag keeps the modern builder happy.

#[vertex]

// void main() {
//     gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
// }

/* clang-format off */
#[fragment]

#ifndef OMIT_DUMMY_MAIN
void main() {
#ifndef USE_TRANSFORM_FEEDBACK
    gl_FragColor = vec4(0.0);
#endif
}
#endif
/* clang-format on */