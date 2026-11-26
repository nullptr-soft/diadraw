//! A small command line over the bindings:
//!
//! ```text
//! cargo run --example diadraw -- check  corpus/c4-02-banking.draw
//! cargo run --example diadraw -- render corpus/c4-02-banking.draw banking.png [view]
//! cargo run --example diadraw -- import diagram.mmd
//! ```

use std::path::Path;
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match run(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}

fn run(args: &[String]) -> Result<(), Box<dyn std::error::Error>> {
    let usage = "usage: diadraw check|format|import <file> | render <file> <output> [view]";
    let (command, file) = match args {
        [c, f, ..] => (c.as_str(), Path::new(f)),
        _ => return Err(usage.into()),
    };
    let text = std::fs::read_to_string(file)?;
    match command {
        "check" => println!("{}", diadraw::check(&text, Some(file))?),
        "format" => print!("{}", diadraw::format(&text)?),
        "import" => {
            let options = diadraw::ImportOptions {
                file: Some(file),
                ..Default::default()
            };
            println!("{}", diadraw::import(&text, &options)?);
        }
        "render" => {
            let output = Path::new(args.get(2).ok_or(usage)?);
            let format = diadraw::Format::from_path(output)
                .ok_or("output must be .svg, .png, .pdf, .drawio or .bpmn")?;
            let options = diadraw::RenderOptions {
                file: Some(file),
                view: args.get(3).map(String::as_str),
                scale: None,
            };
            std::fs::write(output, diadraw::render(&text, format, &options)?)?;
        }
        _ => return Err(usage.into()),
    }
    Ok(())
}
