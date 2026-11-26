use diadraw::{check, format, import, layout, parse, print_model, render};
use diadraw::{Document, ErrorKind, Format, ImportFormat, ImportOptions, RenderOptions};

const SOURCE: &str = r#"model {
  a = process "Alpha"
  b = process "Beta"
  a -> b "calls"
}

view main {
  include *
}
"#;

const BROKEN: &str = "model { a = }";

#[test]
fn reports_versions() {
    assert!(!diadraw::version().is_empty());
    assert!(diadraw::abi_version() >= 2);
}

#[test]
fn parses_checks_and_formats() {
    let parsed = parse(SOURCE).unwrap();
    assert!(parsed.contains(r#""diagnostics":[]"#));

    assert!(check(SOURCE, None).unwrap().contains(r#""errors":0"#));
    let broken = check(BROKEN, None).unwrap();
    assert!(broken.contains(r#""severity":"error""#));

    let formatted = format("model {\n a =   process \"A\"\n}\n").unwrap();
    assert!(formatted.contains("a = process \"A\""));
    assert_eq!(format(BROKEN).unwrap_err().kind(), ErrorKind::Rejected);

    let err = parse("a\0b").unwrap_err();
    assert_eq!(err.kind(), ErrorKind::InvalidArgument);
}

#[test]
fn lays_out_and_renders() {
    let geometry = layout(
        SOURCE,
        &RenderOptions {
            view: Some("main"),
            ..Default::default()
        },
    )
    .unwrap();
    assert!(geometry.contains(r#""nodes":{"a":"#));

    let options = RenderOptions::default();
    let svg = String::from_utf8(render(SOURCE, Format::Svg, &options).unwrap()).unwrap();
    assert!(svg.starts_with("<svg") && svg.contains("Alpha"));

    let png = render(
        SOURCE,
        Format::Png,
        &RenderOptions {
            scale: Some(1.0),
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(&png[..4], b"\x89PNG");
    assert!(render(SOURCE, Format::Pdf, &options)
        .unwrap()
        .starts_with(b"%PDF-"));
    let drawio = render(SOURCE, Format::Drawio, &options).unwrap();
    assert!(String::from_utf8(drawio).unwrap().contains("<mxfile"));
    assert!(!render(SOURCE, Format::Bpmn, &options).unwrap().is_empty());

    let missing = render(
        SOURCE,
        Format::Svg,
        &RenderOptions {
            view: Some("nope"),
            ..Default::default()
        },
    );
    assert_eq!(missing.unwrap_err().kind(), ErrorKind::NotFound);
    assert_eq!(
        render(BROKEN, Format::Svg, &options).unwrap_err().kind(),
        ErrorKind::Rejected
    );
}

#[test]
fn imports_other_formats() {
    let imported = import(
        "flowchart LR\n  A[Start] --> B[End]\n",
        &ImportOptions {
            format: Some(ImportFormat::Mermaid),
            ..Default::default()
        },
    )
    .unwrap();
    assert!(imported.contains(r#""source":"#) && imported.contains("Start"));

    let unknown = import(
        "hello",
        &ImportOptions {
            file: Some("notes.txt".as_ref()),
            ..Default::default()
        },
    );
    assert_eq!(unknown.unwrap_err().kind(), ErrorKind::InvalidArgument);
}

#[test]
fn edits_documents_with_undo() {
    let mut doc = Document::from_source(SOURCE).unwrap();
    doc.apply(r#"{"type":"addElement","element":{"id":"c","kind":"process","name":"Gamma"}}"#)
        .unwrap();
    assert!(doc.source().contains("c = process \"Gamma\""));
    let dup = doc.apply(r#"{"type":"addElement","element":{"id":"c","kind":"process"}}"#);
    assert_eq!(dup.unwrap_err().kind(), ErrorKind::Rejected);
    assert!(doc.can_undo());
    assert!(doc.undo());
    assert!(!doc.source().contains("Gamma"));
    assert!(doc.redo());
    assert!(doc.resolve_view("main").unwrap().contains(r#""id":"c""#));
    assert_eq!(
        doc.resolve_view("nope").unwrap_err().kind(),
        ErrorKind::NotFound
    );

    let empty = Document::new();
    assert!(print_model(&empty.model_json()).is_ok());
    assert!(Document::from_json("[]").is_err());
    assert_eq!(
        Document::from_source(BROKEN).unwrap_err().kind(),
        ErrorKind::Rejected
    );
}

#[test]
fn calls_from_many_threads() {
    let handles: Vec<_> = (0..8)
        .map(|_| {
            std::thread::spawn(|| render(SOURCE, Format::Svg, &RenderOptions::default()).unwrap())
        })
        .collect();
    for h in handles {
        assert!(!h.join().unwrap().is_empty());
    }
    let doc = Document::from_source(SOURCE).unwrap();
    std::thread::spawn(move || assert!(doc.source().contains("Alpha")))
        .join()
        .unwrap();
}

#[test]
fn refuses_png_scales_it_cannot_draw() {
    for scale in [9.0, f64::INFINITY, f64::NAN] {
        let options = RenderOptions {
            scale: Some(scale),
            ..Default::default()
        };
        assert_eq!(
            render(SOURCE, Format::Png, &options).unwrap_err().kind(),
            ErrorKind::InvalidArgument
        );
    }
}
