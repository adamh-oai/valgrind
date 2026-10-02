use std::ffi::CString;
use std::fs::File;
use std::io::Write;
use valgrind_requests::{callgrind, valgrind};

#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn lackey_rust_target(destination: *mut usize) {
    destination.write_volatile(17);
}

fn command(text: &str) {
    assert!(
        valgrind::monitor_command(CString::new(text).unwrap()),
        "Lackey rejected {text}"
    );
}

fn main() {
    assert!(
        valgrind::running_on_valgrind() > 0,
        "run this fixture under Lackey"
    );
    let mut slots = Box::new([0_usize; 6]);
    let mut manifest = File::create("rust-requests.addresses").unwrap();
    command("lackey function add lackey_rust_target");
    // The volatile store is inlined from core::ptr, so its source line can
    // belong to Rust's standard library even inside this exported function.
    command("lackey file add *");
    command("lackey output rust-requests.trace");
    unsafe { lackey_rust_target(&mut slots[0]) };
    command("lackey instrumentation on");
    unsafe { lackey_rust_target(&mut slots[1]) };
    callgrind::stop_instrumentation();
    unsafe { lackey_rust_target(&mut slots[2]) };
    callgrind::start_instrumentation();
    command("lackey output rust-requests-second.trace");
    unsafe { lackey_rust_target(&mut slots[3]) };
    command("lackey function clear");
    command("lackey function add does_not_match");
    unsafe { lackey_rust_target(&mut slots[4]) };
    command("lackey instrumentation off");
    unsafe { lackey_rust_target(&mut slots[5]) };
    for (index, slot) in slots.iter().enumerate() {
        writeln!(
            manifest,
            "rust-requests.trace\t{index}\t{slot:p}\t{}",
            usize::from(index == 1)
        )
        .unwrap();
        writeln!(
            manifest,
            "rust-requests-second.trace\t{index}\t{slot:p}\t{}",
            usize::from(index == 3)
        )
        .unwrap();
    }
    println!("complete");
}
