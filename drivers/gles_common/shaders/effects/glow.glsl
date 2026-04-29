#[modes]
mode_default =

#[vertex]
void main() {
    // Satisfies the compiler's requirement for vertex geometry
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}

#[fragment]
void main() {
    // Satisfies the compiler's requirement for a fragment output
    gl_FragColor = vec4(0.0);
}