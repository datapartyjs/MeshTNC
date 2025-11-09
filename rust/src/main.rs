use std::path::PathBuf;
use clap::Parser;
use meshcore::identity::KeystoreInput;
use tokio_serial::available_ports;
use color_eyre::eyre::Result;
use pretty_env_logger;
use log::debug;

use r_extcap::{cargo_metadata, ExtcapArgs, ExtcapStep, interface::*, controls::*, config::*};
use pcap_file::pcap::{PcapHeader, PcapPacket, PcapWriter};
use lazy_static::lazy_static;

use meshtnc::MeshTNC;
use tokio_util::bytes::Bytes;

#[derive(Debug, Parser)]
struct AppArgs {
    #[command(flatten)]
    extcap: ExtcapArgs,

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

lazy_static! {
    pub static ref CONFIG_PORT: StringConfig = StringConfig::builder()
        .config_number(0)
        .call("port")
        .display("Serial Port")
        .tooltip("TMeshTNC Kiss-compatible device port")
        .placeholder("/dev/ttyUSB0")
        .build();
    pub static ref CONFIG_FREQ: DoubleConfig = DoubleConfig::builder()
        .config_number(1)
        .call("freq")
        .display("Radio Frequency")
        .tooltip("On-the-air frequency")
        .default_value(910.525)
        .build();
    pub static ref CONFIG_BW: RadioConfig = RadioConfig::builder()
        .config_number(2)
        .call("bw")
        .display("Radio bandwidth (kHz)")
        .tooltip("Radio modulation bandwidth in khz.")
        .options([
            ConfigOptionValue::builder()
                .value("62.5")
                .display("62.5 kHz")
                .default(true)
                .build(),
            ConfigOptionValue::builder()
                .value("125")
                .display("125 kHz")
                .build(),
            ConfigOptionValue::builder()
                .value("250")
                .display("250 kHz")
                .build(),
            ConfigOptionValue::builder()
                .value("500")
                .display("500 kHz")
                .build(),
        ])
        .build();
    pub static ref CONFIG_SF: IntegerConfig = IntegerConfig::builder()
        .config_number(3)
        .call("sf")
        .display("Spreading Factor (SF)")
        .tooltip("Modulation spreading factor")
        .range(5..=12)
        .default_value(7)
        .build();
    pub static ref CONFIG_CR: IntegerConfig = IntegerConfig::builder()
        .config_number(4)
        .call("cr")
        .display("Coding Rate (CR)")
        .tooltip("Modulation coding rate")
        .range(5..=8)
        .default_value(5)
        .build();
    
    pub static ref METADATA: Metadata = Metadata {
        help_url: "http://housedillon.com/meshcore-wireshark".into(),
        display_description: "MeshCore Monitor for MeshTNC".into(),
        ..r_extcap::cargo_metadata!()
    };

    static ref INTERFACE: Interface = Interface::builder()
        .value("mc0".into())
        .display("MeshCore Monitor".into())
        .dlt(Dlt::builder()
            .display("User0".into())
            .name("MESHCORE".into())
            .data_link_type(DataLink::USER0)
            .build())
        .build();
            
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

    // Process the extcap arguments
    Ok(match args.extcap.run()? {
        ExtcapStep::Interfaces(interfaces_step) => {
            interfaces_step.list_interfaces(
                &METADATA,
                &[
                    &INTERFACE
                ],
                &[
                ],
            );
        }

        ExtcapStep::Dlts(dlts_step) => {
            dlts_step.print_from_interfaces(&[
                &INTERFACE
            ])?;
        }

        ExtcapStep::Config(config_step) => config_step.list_configs(&[
            &*CONFIG_PORT,
            &*CONFIG_FREQ,
            &*CONFIG_BW,
            &*CONFIG_SF,
            &*CONFIG_CR
            ]),

        ExtcapStep::ReloadConfig(reload_config_step) => {
            reload_config_step.reload_from_configs(&[
            ])?;
        }

        ExtcapStep::Capture(capture_step) => {
            let pcap_header = PcapHeader {
                datalink: DataLink::USER0,
                endianness: pcap_file::Endianness::Big,
                ..Default::default()
            };

            let pcap_writer = PcapWriter::with_header(capture_step.fifo, pcap_header)?;

            let result = tnc.start(Some(pcap_writer)).await;
            if let Err(e) = result {
                return Err(e);
            }            
        }
    })
}
