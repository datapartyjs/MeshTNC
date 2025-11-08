use color_eyre::eyre::eyre;
use futures::{SinkExt, StreamExt, lock::Mutex};
use log::{debug, error, info, trace, warn};
use tokio_serial::{self, SerialPort, SerialPortBuilderExt, SerialStream};
use tokio_util::{bytes::{Bytes, BytesMut}, codec::{Decoder, Encoder}};
use std::{fmt::Display, io, sync::{Arc, atomic::Ordering}, time::Duration};
use meshcore::{identity::Keystore, packet::Packet};

use atomic_enum::atomic_enum;

struct MeshRawPacket {
    rssi: Option<f32>,
    snr: Option<f32>,
    port: Option<i8>,

    contents: Packet
}

#[atomic_enum]
#[derive(PartialEq)]
enum MeshTncState {
    Unknown  = 0,
    Starting = 1,
    RadioSet = 2,
    Idle     = 3,
    KISS     = 4,
}

impl Display for MeshTncState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            MeshTncState::Unknown =>  write!(f, "Unknown"),
            MeshTncState::Starting => write!(f, "Starting"),
            MeshTncState::RadioSet => write!(f, "RadioSet"),
            MeshTncState::Idle     => write!(f, "Idle"),
            MeshTncState::KISS     => write!(f, "KISS"),
        }
    }
}

struct KissCodec {
    state: Arc<AtomicMeshTncState>
}

impl Default for KissCodec {
    fn default() -> Self {
        Self { state:
            Arc::new(AtomicMeshTncState::new(MeshTncState::Starting))
        }
    }
}

impl KissCodec {
    fn decode_ascii(&mut self, src: &mut BytesMut) -> Result<Option<Vec<u8>>, io::Error> {
        // trace!("In decode_ascii: {}", str::from_utf8(src.as_ref()).unwrap());
        let newline = src.as_ref().iter().position(|b| *b == b'\n');
        if let Some(n) = newline {
            let line = src.split_to(n + 1);
            return match str::from_utf8(line.as_ref()) {
                Ok(s) => {
                    trace!("Got ascii line: {}", s.trim_ascii());
                    Ok(Some(s.into()))
                },
                Err(_) => Err(
                    io::Error::new(io::ErrorKind::Other, "Invalid String")),
            };
        } else {
            Ok(None)
        }
    }

    fn decode_kiss(&mut self, _src: &mut BytesMut) -> Result<Option<Vec<u8>>, io::Error> {
        Ok(None)
    }

    fn encode_ascii(&self, src: Vec<u8>, dst: &mut BytesMut) -> Result<(), io::Error> {
        // Append the new string with a newline into the BytesMut
        let string = String::from_utf8(src);
        if let Err(e) = string {
            warn!("Tried to send illegal string over port: {}", e);
            return Err(io::Error::new(io::ErrorKind::Other, "Invalid string"));
        } else if let Ok(string) = string {
            let newline_string = format!("{}\n\r", string);
            let string_length = newline_string.as_bytes().len();
            dst.reserve(string_length);
            dst.extend_from_slice(newline_string.as_bytes());
            Ok(())
        } else {
            // INCONCEVABLE!
            Ok(())
        }
    }

    fn encode_kiss(&self, _src: Vec<u8>, _dst: &mut BytesMut) -> Result<(), io::Error> {
        Ok(())
    }
}

impl Decoder for KissCodec {
    type Item = Vec<u8>;
    type Error = io::Error;

    fn decode(&mut self, src: &mut BytesMut) -> Result<Option<Self::Item>, Self::Error> {
        // This function gets called periodically, and if there's a frame it's able
        // to discover within the byte stream then it returns Ok(Self::Item) with the
        // contents after splitting that data off the head of the buffer.
        // 
        // If no frames are discovered within the stream, then it returns Ok(None).
        //
        // Because the KISS system can either be ASCII when in non-KISS mode, and KISS
        // doesn't use regular line endings, we have to keep track of what state we think
        // we're in.
        let state = self.state.load(Ordering::Relaxed);
        match state {
            MeshTncState::Starting => return self.decode_ascii(src),
            MeshTncState::RadioSet => return self.decode_ascii(src),
            MeshTncState::Idle     => return self.decode_ascii(src),
            MeshTncState::KISS     => return self.decode_kiss(src),

            // If we're in an unknown state, it's really best to just start discarding
            // bytes.  We should know somewhere "above" this code that we need help,
            // and that place should either attempt to reboot the device or send an
            // escape sequence that lets us get into either Starting or Idle.
            MeshTncState::Unknown => {
                src.clear();
                return Ok(None);
            }
        }
    }
}

impl Encoder<Vec<u8>> for KissCodec {
    type Error = io::Error;

    fn encode(&mut self, mut item: Vec<u8>, dst: &mut BytesMut) -> Result<(), Self::Error> {
        // Depending on what state we're in, we need to make choices about how to
        // frame the message.
        let state = self.state.load(Ordering::Relaxed);
        match state {
            MeshTncState::Starting => return self.encode_ascii(item, dst),
            MeshTncState::RadioSet => return self.encode_ascii(item, dst),
            MeshTncState::Idle     => return self.encode_ascii(item, dst),
            MeshTncState::KISS     => return self.encode_kiss(item, dst),

            // If we're in an unknown state, it's really best to just start discarding
            // bytes.  We should know somewhere "above" this code that we need help,
            // and that place should either attempt to reboot the device or send an
            // escape sequence that lets us get into either Starting or Idle.
            MeshTncState::Unknown => {
                item.clear();
                return Ok(());
            }
        }
    }
}

pub struct MeshTNC {
    pub port: Mutex<SerialStream>,
    pub freq: f32,
    pub bw: f32,
    pub sf: u8,
    pub cr: u8,
    pub sb: String,
    pub keystore: Keystore
}

impl MeshTNC {
    pub fn new(port: String, freq: f32, bw: f32, sf: u8, cr: u8, sb: String, keystore: Keystore) -> color_eyre::eyre::Result<MeshTNC> {

        // Attempt to open the port
        let mut port = tokio_serial::new(port, 115_200)
            .flow_control(tokio_serial::FlowControl::None)
            .stop_bits(tokio_serial::StopBits::One)
            .data_bits(tokio_serial::DataBits::Eight)
            .open_native_async()?;

        // We really don't need exclusive access to this port.
        // This will allow us to see what's happening with other
        // users of this port are running.
        #[cfg(unix)]
        if let Err(e) = port.set_exclusive(false) {
            warn!("Unable to set non-exslusive use of this port. ¯\\_(ツ)_/¯: {}", e.description);
        }

        Ok(MeshTNC {
            port: Mutex::new(port),
            freq,
            bw,
            sf,
            cr,
            sb,
            keystore,
        })
    }

    pub async fn start(&mut self) -> color_eyre::eyre::Result<()> {
        // get protected access to the port
        let port = self.port.get_mut();

        // This doesn't work on the Seeed Studio LoRa-E5 Dev Board
        // because the rts pin is capactively-coupled to reset, and the
        // capacitor is way too small.  So, the reset pulse is too small
        // and too short to actually work.
        info!("Attempting to reboot TNC");
        port.write_request_to_send(false)?;
        tokio::time::sleep(Duration::from_millis(10)).await;
        port.write_request_to_send(true)?;

        // Create the framed reader
        let codec = KissCodec::default();
        // Get a copy of the state from the reader (needs to be Arc<Atomic> because
        // we need to share ownershil with the codec system.)
        let local_state = Arc::clone(&codec.state);
        // Create the reader codec framing system
        let framed = codec.framed(port);
        let (mut writer, mut reader) = framed.split();

        // Handle incoming bytes from the interface
        while let Some(line_result) = reader.next().await {
            let line = line_result.expect("Failed to read line");

            // Depending on the mode we're in, we want to handle them differently
            // In the "starting" mode, we're waiting for the "Starting!" message
            // from the device.

            // First match just KISS because the rest are all ASCII
            let state = local_state.load(Ordering::Relaxed);

            if state == MeshTncState::KISS {
                // Just handle the packet
            }
            
            else {
                // Convert it to ascii and trim any whitespace
                let line = String::from_utf8(line)?;

                match state {
                // This is the origin state where we assume the device has just
                // booted.  We look for "Starting!" and move into idle.
                MeshTncState::Starting => {
                    if line.trim_ascii() == "Starting!" {
                        // Send the command to set the radio data.
                        let radio_set = format!(
                            "set radio {},{},{},{},{}",
                            self.freq,
                            self.bw,
                            self.sf,
                            self.cr,
                            self.sb
                        );
                        // Send the radio parameters
                        writer.send(radio_set.as_bytes().to_vec()).await?;
                        local_state.store(MeshTncState::RadioSet, Ordering::Relaxed);
                        debug!("Moved into the Radio Set state")
                    }
                }
                MeshTncState::RadioSet => {
                    // Ensure that the radio parameters were accepted.  We'll
                    // scan until we get either something that contains "OK"
                    // or an "Error".  In the case of "Error", the radio didn't
                    if line.contains("OK") {
                        // Move onto the "idle" state.
                        local_state.store(MeshTncState::Idle, Ordering::Relaxed);
                        debug!("Moved into idle state");
                        info!("Radio ready!");
                        println!(" ROUTE T | v1 | Transp Fl. |     Route Summary    | I | Pkt. Type | Summary....");
                    } else if line.contains("Error") {
                        // This error can't be helped by us, so kick it back to the user
                        // with a hopefully helpful error
                        error!("Radio parameters weren't accepted by the TNC.  Please check them.");
                        return Err(eyre!("Unable to set radio parameters; they were rejected by the TNC."));
                    }
                }
                MeshTncState::Idle => {
                    // In the idle state, the TNC will send us radio log packets.
                    
                    // If the user didn't request KISS mode (which allows transmit, but no RSSI/SNR)
                    // then we're kinda done, and we can start sending packets into the channel
                    let components: Vec<&str> = line.trim_ascii().split(",").collect();
                    if components[1] == "RXLOG" {
                        if components.len() == 5 {
                            let _timestamp: Result<u64, _> = components[0].parse();
                            // Skipping "RXLOG"
                            let rssi: Option<f32> = components[2].parse().ok();
                            let snr: Option<f32> = components[3].parse().ok();
                            let data: Result<Vec<u8>, _> = hex::decode(components[4]);

                            if let Ok(data) = data {
                                debug!("Got packet: {}", hex::encode_upper(&data));
                                let mut packet = MeshRawPacket {
                                    rssi,
                                    snr,
                                    port: None,
                                    contents: Packet::from(Bytes::copy_from_slice(&data))
                                };
                                packet.contents.try_decrypt(&self.keystore);
                                println!("{}", packet.contents);

                            } else if let Err(e) = data {
                                debug!("Got RXLOG packet with invalid HEX: \"{}\": {}", components[4], e);
                            }

                            continue;
                        }
                        debug!("Got a potentially mal-formed RXLOG packet: \n{}", line);
                    }
                },
                // We'll never get here, because we handle it up there ^
                MeshTncState::KISS => break,
                // We'll never get here, because bytes in `unknown`
                // are all discarded.
                MeshTncState::Unknown => break,
            }

            }
        }

        Ok(())
   }
}