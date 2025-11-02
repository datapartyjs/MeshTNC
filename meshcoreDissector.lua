-- MeshCore packet Dissector
-- declare our protocol
meshcore_proto = Proto("meshcore","MeshCore datalink")

-- Header fields
route_type      = ProtoField.uint8("meshcore.route_type", "route_type", base.HEX)
transport_code1 = ProtoField.uint16("meshcore.transport_code1", "transport_code1", base.HEX)
transport_code2 = ProtoField.uint16("meshcore.transport_code2", "transport_code2", base.HEX)
payload_type    = ProtoField.uint8("meshcore.payload_type", "payload_type", base.HEX)
path_len        = ProtoField.uint8("meshcore.path_len", "Path Length", base.DEC)
path            = ProtoField.bytes("path", "Path", base.SPACE)

-- Payload fields
payload      = ProtoField.bytes("pd", "Data",         base.NONE)
public_key   = ProtoField.bytes("pkey", "Public Key", base.NONE)
timestamp    = ProtoField.absolute_time("ts", "Timestamp", base.UTC)
signature    = ProtoField.bytes("sig", "Signature", base.NONE)
adv_flags    = ProtoField.uint8("af", "Advertisement Flags", base.HEX)
node_type    = ProtoField.string("nt", "Node Type", "Node type such as Node, Repeater, Room, or Sensor")
latitude     = ProtoField.float("dlat", "Latitude", "Degrees Latitude")
longitude    = ProtoField.float("dlon", "Longitude", "Degrees Longitude")
feature      = ProtoField.uint16("ft", "Feature", base.HEX)
name         = ProtoField.string("name", "Node Name")
csum         = ProtoField.bytes("csum", "Checksum", base.NONE)
source       = ProtoField.uint8("src", "Source", base.HEX)
destination  = ProtoField.uint8("dst", "Destination", base.HEX)
cypher_mac   = ProtoField.bytes("cmac", "Cypher MAC", base.NONE)
cypher_text  = ProtoField.bytes("ctext", "Cypher Text", base.NONE)

meshcore_proto.fields = {
  route_type,
  transport_code1,
  transport_code2,
  payload_type,
  path_len,
  path,
  payload,
  public_key,
  timestamp,
  signature,
  adv_flags,
  node_type,
  latitude,
  longitude,
  feature,
  name,
  csum,
  source,
  destination,
  cypher_mac,
  cypher_text,
}

function get_route_type(number)
      if number == 0 then route_type = "Transport Flood:"
  elseif number == 1 then route_type = "Flood:"
  elseif number == 2 then route_type = "Direct:"
  elseif number == 3 then route_type = "Transport Direct:"
  else                    route_type = "Parsing error: (" .. number .. ")"
  end

  return route_type
end

function get_payload_type(number)
      if number == 0x00 then payload_type = "Request:"
  elseif number == 0x01 then payload_type = "Response:"
  elseif number == 0x02 then payload_type = "Text Message:"
  elseif number == 0x03 then payload_type = "Ack:"
  elseif number == 0x04 then payload_type = "Advertisement:"
  elseif number == 0x05 then payload_type = "Group Text:"
  elseif number == 0x06 then payload_type = "Group Data:"
  elseif number == 0x07 then payload_type = "Anonymous Request:"
  elseif number == 0x08 then payload_type = "Path:"
  elseif number == 0x09 then payload_type = "Trace:"
  elseif number == 0x0A then payload_type = "Multipart:"
  elseif number == 0x0F then payload_type = "Raw (custom encryption):"
  else                       payload_type = "Parsing error: (" .. number .. ")"
  end

  return payload_type
end

function meshcore_path_buff(tree, buffer, route_type_number, payload_version, path_len_number)
  local offset = 0
  local path_len_number = buffer(offset, 1):le_uint()
  offset = offset + 1
  local subtree = tree:add(meshcore_proto, buffer(0, path_len_number + 1), "Path")

  -- Get path information if it's not a flood
  subtree:add_le(path_len, path_len_number, buffer(0, 1))

  if route_type_number & 0x20 then
    if path_len_number > 0 then
      subtree:add(path, buffer(offset, path_len_number))
      return 1 + path_len_number
    end
  end
  return 1
end

function meshcore_cypher_packet(buffer, pinfo, subtree)
  pinfo.cols.dst = string.format("%02x", buffer(0, 1):uint())
  subtree:add(destination, buffer(0, 1))
  pinfo.cols.src = string.format("%02x", buffer(1, 1):uint())
  subtree:add(source,      buffer(1, 1))
  subtree:add(cypher_mac,  buffer(2, 2))
  subtree:add(cypher_text, buffer(4, buffer:len() - 4))
end

function meshcore_req(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Request")
  pinfo.cols.info = "Request"
  meshcore_cypher_packet(buffer, pinfo, subtree)
end

function meshcore_resp(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Response")
  pinfo.cols.info = "Response"
  meshcore_cypher_packet(buffer, pinfo, subtree)
end

function meshcore_text(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Plain Text")
  pinfo.cols.info = "Plain Text"
  meshcore_cypher_packet(buffer, pinfo, subtree)
end

function meshcore_ack(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Acknowledgement")
  pinfo.cols.info = "Acknowledgement"
  subtree:add(checksum, buffer(0, 4))
end

function meshcore_advert(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Node Advertisement")
  pinfo.cols.info = "Advertisement"
  pinfo.cols.src = string.format("%02x", buffer(0, 1):uint())
  subtree:add(   public_key, buffer(0, 32))
  subtree:add_le(timestamp,  buffer(32, 4))
  subtree:add(   signature,  buffer(36,64))
  local offset = 32 + 4 + 64

  local appdata_subtree = subtree:add(meshcore_proto, buffer(offset, buffer:len() - offset), "Appdata")

  local flags = buffer(offset, 1):uint()
  offset = offset + 1

  -- No, this isn't a bug, these node types are not exclusive
  if flags & 0x01 == 0x01 then appdata_subtree:add(node_type, "Client") end
  if flags & 0x02 == 0x02 then appdata_subtree:add(node_type, "Repeater") end
  if flags & 0x03 == 0x03 then appdata_subtree:add(node_type, "Room") end
  if flags & 0x04 == 0x04 then appdata_subtree:add(node_type, "Sensor") end

  -- This advertisement is expected to have a location set
  if flags & 0x10 == 0x10 then
    appdata_subtree:add(latitude, buffer(offset, 4):le_int() / 1000000)
    offset = offset + 4
    appdata_subtree:add(longitude, buffer(offset, 4):le_int() / 1000000)
    offset = offset + 4
  end

  -- This advertisement is expected to have a feature set (reserved)
  if flags & 0x20 == 0x20 then
    appdata_subtree:add(feature, buffer(offset, 2):le_int())
    offset = offset + 2
  end
  if flags & 0x40 == 0x40 then
    appdata_subtree:add(feature, buffer(offset, 2):le_int())
    offset = offset + 2
  end

  -- The remainder of the data is the node name
  if flags & 0x80 == 0x80 then
    appdata_subtree:add(name, buffer(offset, buffer:len() - offset))
    pinfo.cols.info = "Advertisement: " .. buffer(offset, buffer:len() - offset):string()
  end
end

function meshcore_grp_txt(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Group Text")
  pinfo.cols.info = "Group Text"
  pinfo.cols.dst = string.format("%02x", buffer(0, 1):uint())
  subtree:add(destination, buffer(0, 1))
  subtree:add(cypher_mac,  buffer(1, 2))
  subtree:add(cypher_text, buffer(3, buffer:len() - 3))
end

function meshcore_grp_data(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Group Data")
  pinfo.cols.info = "Group Data"
  pinfo.cols.dst = string.format("%02x", buffer(0, 1):uint())
  subtree:add(destination, buffer(0, 1))
  subtree:add(cypher_mac,  buffer(1, 2))
  subtree:add(cypher_text, buffer(3, buffer:len() - 3))
end

function meshcore_anon_req(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Anonymous Request")
  pinfo.cols.info = "Anon Req."
  pinfo.cols.dst = string.format("%02x", buffer(0, 1):uint())
  subtree:add(destination, buffer( 0,  1))
  subtree:add(public_key,  buffer( 1, 32))
  subtree:add(cypher_mac,  buffer(33,  2))
  subtree:add(cypher_text, buffer(35, buffer:len() - 35))
end

function meshcore_path(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Returned Path")
  pinfo.cols.info = "Returned path"
  meshcore_cypher_packet(buffer, pinfo, subtree)
end

function meshcore_trace(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Trace (undocumented)")
  pinfo.cols.info = "Trace"
  subtree:add_le(payload, buffer)
end

function meshcore_multipart(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Multipart packet (undocumented)")
  pinfo.cols.info = "Multipart"
  subtree:add_le(payload, buffer)
end

function meshcore_raw(buffer, pinfo, tree)
  local subtree = tree:add(meshcore_proto, buffer(), "Raw packet")
  pinfo.cols.info = "Raw"
  subtree:add_le(payload, buffer)
end

function meshcore_payload(buffer, pinfo, tree, type)
      if type == 0x00 then meshcore_req(buffer, pinfo, tree)
  elseif type == 0x01 then meshcore_resp(buffer, pinfo, tree)
  elseif type == 0x02 then meshcore_text(buffer, pinfo, tree)
  elseif type == 0x03 then meshcore_ack(buffer, pinfo, tree)
  elseif type == 0x04 then meshcore_advert(buffer, pinfo, tree)
  elseif type == 0x05 then meshcore_grp_txt(buffer, pinfo, tree)
  elseif type == 0x06 then meshcore_grp_data(buffer, pinfo, tree)
  elseif type == 0x07 then meshcore_anon_req(buffer, pinfo, tree)
  elseif type == 0x08 then meshcore_path(buffer, pinfo, tree)
  elseif type == 0x09 then meshcore_trace(buffer, pinfo, tree)
  elseif type == 0x0A then meshcore_multipart(buffer, pinfo, tree)
  elseif type == 0x0F then meshcore_raw(buffer, pinfo, tree)
  else
    local subtree = tree:add(meshcore_proto, buffer(), "Unknown Payload: (" .. type .. ")")
    pinfo.cols.info = "Unknown (" .. type .. ")"
    subtree:add_le(payload, buffer)
  end
end

function meshcore_proto.dissector(buffer, pinfo, tree)
  length = buffer:len()
  if length == 0 then return end
  local offset = 0

  pinfo.cols.protocol = meshcore_proto.name

  -- Get the header packet type from the header fields
  local header = buffer(0,1):le_uint()
  offset = offset + 1

  -- "payload" version because it's really the header version.
  local payload_version = (header & 0xC0) >> 6

  if payload_version == 0 then
    subtree = tree:add(meshcore_proto, buffer(0, 1), "MeshCore Headers v. 1")
  elseif payload_version == 1 then
    subtree = tree:add(meshcore_proto, buffer(0, 1), "MeshCore Headers v. 2")
  else
    local subtree = tree:add(meshcore_proto, buffer(), "MeshCore Headers unknown version")
    subtree:add_le(payload, "Payload", buffer)
    return
  end

  local route_type_number = header & 0x03
  local route_type_name   = get_route_type(route_type_number)
  subtree:add_le(route_type, route_type_number)

  -- Get the transport code is this is a transport route type
  if route_type_number == 0x00 or route_type_number == 0x03 then
    offset = offset + 4
    subtree:add_le(transport_code1, buffer(1, 2):le_uint())
    subtree:add_le(transport_code2, buffer(3, 2):le_uint())
  end

  local payload_type_number = (header & 0x3C) >> 2
  local payload_type_name = get_payload_type(payload_type_number)
  subtree:add_le(payload_type, payload_type_number)

  offset = offset + meshcore_path_buff(subtree, buffer(offset, length - offset), route_type_number, payload_version, path_len_number)
  
  -- Dispatch and print payloads
  local packet_payload_buffer = buffer(offset, length - offset)
  meshcore_payload(packet_payload_buffer, pinfo, subtree, payload_type_number)

end

local wtap_encap_table = DissectorTable.get("wtap_encap")
wtap_encap_table:add(wtap.USER0, meshcore_proto)