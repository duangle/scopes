
let vec2 = (vector f32 2:usize)
let vec4 = (vector f32 4:usize)

let gl_Position = (extern 'spirv.Position vec4 'Output)
let gl_VertexID = (extern 'spirv.VertexId i32 'Input)

let sin = (extern 'glsl.std.450.Sin (function f32 f32))

fn set-vertex-position ()
    let screen-tri-vertices =
        arrayof vec2
            vectorof f32 -1 -1
            vectorof f32  3 -1
            vectorof f32 -1  3
    let pos = (screen-tri-vertices @ gl_VertexID)
    gl_Position = (vectorof f32 (pos @ 0) (pos @ 1) 0 1)
    pos

let vertex-code =
    do
        let uv = (extern 'uv vec2 'Output 'location 0)
        fn vertex-shader ()
            let half = (vectorof f32 0.5 0.5)
            uv =
                ((set-vertex-position) * half) + half
            return;

        #dump-label
            typify vertex-shader

        let code =
            compile-glsl 'vertex-stage
                typify vertex-shader
                #'dump-disassembly
                #'no-opts
        print code
        code

let fragment-code =
    do
        let uv = (extern 'uv vec2 'Input 'location 0)
        let out_Color = (extern 'out_Color vec4 'Output)
        let phase = (extern 'phase f32 'UniformConstant 'location 0)
        let tex =
            extern 'tex
                SampledImage-type
                    Image-type vec4 '2D 0 0 0 1 'Unknown unnamed
                \ 'UniformConstant 'location 1
        fn make-phase ()
            #if ((load phase) < 0.5)
                unconst 0.0
            #else
                unconst 1.0
            (sin phase) * 0.5 + 0.5
        fn fragment-shader ()
            let uv = (load uv)
            let color = (vectorof f32 (uv @ 0) (uv @ 1) (make-phase) 1)
            let s = (sample (load tex) uv)
            out_Color = (fmul color s)
            return;

        #dump-label (Closure-label fragment-shader)

        #dump-label
            typify fragment-shader

        let code =
            compile-glsl 'fragment-stage
                typify fragment-shader
                #'dump-disassembly
                #'no-opts
        print code
        code
