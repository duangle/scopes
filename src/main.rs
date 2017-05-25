
//use std::io;
#[allow(unused_imports)]
use std::io::prelude::*;
use std::fs::File;
use std::path::PathBuf;
use std::collections::HashMap;

//------------------------------------------------------------------------------
// ANSI COLOR FORMATTING
//------------------------------------------------------------------------------

#[allow(dead_code, non_snake_case)]
mod ANSI {
    pub static RESET          : &'static str = "\x1b[0m";
    pub static COLOR_BLACK    : &'static str = "\x1b[30m";
    pub static COLOR_RED      : &'static str = "\x1b[31m";
    pub static COLOR_GREEN    : &'static str = "\x1b[32m";
    pub static COLOR_YELLOW   : &'static str = "\x1b[33m";
    pub static COLOR_BLUE     : &'static str = "\x1b[34m";
    pub static COLOR_MAGENTA  : &'static str = "\x1b[35m";
    pub static COLOR_CYAN     : &'static str = "\x1b[36m";
    pub static COLOR_GRAY60   : &'static str = "\x1b[37m";

    pub static COLOR_GRAY30   : &'static str = "\x1b[30;1m";
    pub static COLOR_XRED     : &'static str = "\x1b[31;1m";
    pub static COLOR_XGREEN   : &'static str = "\x1b[32;1m";
    pub static COLOR_XYELLOW  : &'static str = "\x1b[33;1m";
    pub static COLOR_XBLUE    : &'static str = "\x1b[34;1m";
    pub static COLOR_XMAGENTA : &'static str = "\x1b[35;1m";
    pub static COLOR_XCYAN    : &'static str = "\x1b[36;1m";
    pub static COLOR_WHITE    : &'static str = "\x1b[37;1m";

    pub fn COLOR_RGB(prefix : &str, hexcode : u32) -> String {
        prefix.to_string()
            + &(hexcode >> 16 & 0xff).to_string() + ";"
            + &(hexcode >> 8 & 0xff).to_string() + ";"
            + &(hexcode & 0xff).to_string() + "m"
    }

    pub fn COLOR_RGB_BG(hexcode : u32) -> String { COLOR_RGB("\x1b[48;2;", hexcode) }
    pub fn COLOR_RGB_FG(hexcode : u32) -> String { COLOR_RGB("\x1b[38;2;", hexcode) }

    pub fn supported() -> bool {
        // TODO: (C.isatty(C.fileno(C.stdout)) ~= 0)
        true
    }   
}

#[allow(dead_code, non_snake_case)]
enum Style {
    Symbol,
    String,
    Number,
    Keyword,
    Function,
    SfxFunction,
    Operator,
    Instruction,
    Type,
    Comment,
    Error,
    Location,
}

impl Style {
    // 24-bit ANSI color escapes (ISO-8613-3): 
    // works on most shells as well as windows 10
    #[cfg(feature = "rgbescape")]
    fn to_ansi(self) -> String {
        match self {
            Style::Symbol => ANSI::COLOR_RGB_FG(0xCCCCCC),
            Style::String => ANSI::COLOR_RGB_FG(0xCC99CC),
            Style::Number => ANSI::COLOR_RGB_FG(0x99CC99),
            Style::Keyword => ANSI::COLOR_RGB_FG(0x6699CC),
            Style::Function => ANSI::COLOR_RGB_FG(0xFFCC66),
            Style::SfxFunction => ANSI::COLOR_RGB_FG(0xCC6666),
            Style::Operator => ANSI::COLOR_RGB_FG(0x66CCCC),
            Style::Instruction => ANSI::COLOR_YELLOW.to_string(),
            Style::Type => ANSI::COLOR_RGB_FG(0xF99157),
            Style::Comment => ANSI::COLOR_RGB_FG(0x999999),
            Style::Error => ANSI::COLOR_XRED.to_string(),
            Style::Location => ANSI::COLOR_RGB_FG(0x999999),
        }
    }

    #[cfg(not(feature = "rgbescape"))]
    fn to_ansi(self) -> String {
        match self {
            Style::Symbol => ANSI::COLOR_GRAY60.to_string(),
            Style::String => ANSI::COLOR_XMAGENTA.to_string(),
            Style::Number => ANSI::COLOR_XGREEN.to_string(),
            Style::Keyword => ANSI::COLOR_XBLUE.to_string(),
            Style::Function => ANSI::COLOR_GREEN.to_string(),
            Style::SfxFunction => ANSI::COLOR_RED.to_string(),
            Style::Operator => ANSI::COLOR_XCYAN.to_string(),
            Style::Instruction => ANSI::COLOR_YELLOW.to_string(),
            Style::Type => ANSI::COLOR_XYELLOW.to_string(),
            Style::Comment => ANSI::COLOR_GRAY30.to_string(),
            Style::Error => ANSI::COLOR_XRED.to_string(),
            Style::Location => ANSI::COLOR_GRAY30.to_string(),
        }
    }
}

fn ansi_styler(style : Style, s : &str) -> String { 
    style.to_ansi() + s + ANSI::RESET
}

#[allow(unused_variables)]
fn plain_styler(style : Style, s : &str) -> String { s.to_string() }

static mut _DEFAULT_STYLER : fn(Style, &str) -> String = plain_styler;

fn default_styler(style : Style, s : &str) -> String {
    unsafe { _DEFAULT_STYLER(style, s) }
}

//------------------------------------------------------------------------------
// SYMBOL
//------------------------------------------------------------------------------

#[allow(dead_code)]
static SYMBOL_ESCAPE_CHARS : &'static str = "[]{}()\"";

#[derive(Debug)]
struct Symbol {
    id : u64,
    name : String,
}

struct SymbolTable {
    next_id : u64,
    map : HashMap<String, Symbol>,
}

impl Symbol {
    fn new(name : &str) -> Symbol {
        static mut symtable : SymbolTable = SymbolTable { 
            next_id : 0, 
            map : HashMap::new() 
        };
        unsafe {
            symtable.next_id = symtable.next_id + 1;
            Symbol {
                id : symtable.next_id,
                name : name.to_string()
            }
        }
    }
}

//------------------------------------------------------------------------------
// MAIN
//------------------------------------------------------------------------------

fn get_boot_source_path () -> PathBuf {
    let mut bangra_path = std::env::current_dir()
        .expect("couldn't retrieve executable path");
    bangra_path.push("bangra.b");
    bangra_path
}

#[allow(dead_code)]
fn get_boot_source() -> String {
    let mut file = File::open(get_boot_source_path())
        .expect("couldn't open boot script");
    let mut content  = String::new();
    file.read_to_string(&mut content)
        .expect("couldn't read content");
    content
}

fn main () {
    if ANSI::supported() {
        unsafe { _DEFAULT_STYLER = ansi_styler; }
    }
    //println!("{:?}", get_boot_source());
    println!("{}", default_styler(Style::String, "yo yo yo"));
    println!("{:?}", Symbol::new("test"));
}
