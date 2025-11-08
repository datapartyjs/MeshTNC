use std::path::PathBuf;
use clap::Parser;
use meshcore::identity::KeystoreInput;
use tokio_serial::available_ports;
use color_eyre::eyre::Result;
use pretty_env_logger;
use log::debug;

use meshtnc::MeshTNC;
use tokio_util::bytes::Bytes;

#[derive(Debug, Parser)]
struct AppArgs {
    /// Convenience tool to list serial ports
    #[arg(short, default_value = "false")]
    pub list: bool,

    /// Serial port path
    #[arg(long, default_value = "/dev/ttyUSB0")]
    pub port: String,

    /// Radio frequency
    #[arg(long, default_value = "910.525")]
    pub freq: f32,

    /// Radio Bandwidth
    #[arg(long, default_value = "62.5")]
    pub bw: f32,

    /// Spreading Factor
    #[arg(long, default_value = "7")]
    pub sf: u8,

    /// Coding Rate
    #[arg(long, default_value = "5")]
    pub cr: u8,

    /// Sync Byte
    #[arg(long, default_value = "0x12")]
    pub sb: String,

    #[arg(long, short, help = "Identities file for packet decryption")]
    pub identities_file: Option<PathBuf>,
}

#[tokio::main]
async fn main() -> Result<()> {
    pretty_env_logger::init();
    
    debug!("argv: {:?}", std::env::args());
    let args = AppArgs::parse();
    debug!("Args: {args:?}");

    // Attempt to load the identities file from disk and load all the identities
    let keystore = if let Some(id_path) = args.identities_file {
        let identity_string = std::fs::read_to_string(id_path)?;
        let keystore_in: KeystoreInput = toml::from_str(&identity_string)?;
        keystore_in.compile()
    } else {
        KeystoreInput { identities: vec![], contacts: vec![], groups: vec![] }.compile()
    };

    if args.list {
        let ports = available_ports()?;
        for port in ports.iter() {
            match &port.port_type {
            tokio_serial::SerialPortType::UsbPort(usb_port_info) =>
                if let Some(product) = &usb_port_info.product {
                    println!("Found USB serial port: {}: {}", port.port_name, product);
                } else {
                    println!("Found USB serial port: {}", port.port_name);
                },
            _ => println!("Found serial port: {}", port.port_name),
            }
        }
    }

    // Attempt to create and open the serial device and start the party
    let mut tnc = MeshTNC::new(
        args.port,
        args.freq,
        args.bw,
        args.sf,
        args.cr,
        args.sb,
        keystore,
    )?;

    let result = tnc.start().await;
    if let Err(e) = result {
        Err(e)
    } else {
        Ok(())
    }
}
