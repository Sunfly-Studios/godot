#[modes]
// ....

#[specializations]
// Empty for now, but the tag keeps the modern builder happy.

#[vertex]

void main() {
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}

#[fragment]

void main() {
    gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);
}