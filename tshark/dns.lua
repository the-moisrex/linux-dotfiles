-- How to run the script with tshark:
--   $ tshark -X lua_script:dns.lua
--   $ tshark --color -X lua_script:dns.lua

-- latest development release of Wireshark supports plugin version information
if set_plugin_info then
    local my_info = {
        version   = "1.0",
        author    = "Moisrex",
        email     = "example at something dot com",
        copyright = "Copyright (c) 2023 The Moisrex",
        license   = "MIT license",
        details   = "This plugin will print out the dns queries",
        help      = [[
    HOW TO RUN THIS SCRIPT:
    Wireshark and Tshark support multiple ways of loading Lua scripts: through
    a dofile() call in init.lua, through the file being in either the global
    or personal plugins directories, or via the command line. The latter two
    methods are the best: either copy this script into your "Personal Plugins"
    directory, or load it from the command line.
    ]]
    }
    set_plugin_info(my_info)
end

do
    local time_rel   = Field.new("frame.time_relative")
    local ip_src     = Field.new("ip.src")
    local ip_dst     = Field.new("ip.dst")
    local dns_id     = Field.new("dns.id")
    local qry_name   = Field.new("dns.qry.name")
    local qry_type   = Field.new("dns.qry.type")
    local resp_name  = Field.new("dns.resp.name")
    local resp_type  = Field.new("dns.resp.type")
    local dns_a      = Field.new("dns.a")
    local dns_aaaa   = Field.new("dns.aaaa")
    local dns_cname  = Field.new("dns.cname")
    local dns_flags  = Field.new("dns.flags.rcode")
    local dns_resp   = Field.new("dns.flags.response")

    local record_type_names = {
        [1]  = "A",  [2]  = "NS", [5]  = "CNAME", [6]  = "SOA",
        [15] = "MX", [16] = "TXT", [28] = "AAAA", [33] = "SRV",
    }

    local rcode_names = {
        [0] = "NOERROR", [1] = "FormErr", [2] = "SERVFAIL",
        [3] = "NXDOMAIN", [4] = "NOTIMP", [5] = "REFUSED",
    }

    local tap = Listener.new("dns")

    print("time\tsrc\tdst\tid\tquery\ttype\tresponse")

    function tap.packet(pinfo, tvb, tapdata)
        local t    = tostring(time_rel() or "")
        local src  = tostring(ip_src() or "")
        local dst  = tostring(ip_dst() or "")
        local id   = tostring(dns_id() or "")
        local resp = dns_resp()

        if resp and resp.value == 1 then
            local name = resp_name()
            local rtype = resp_type()
            local rcode = dns_flags()
            local a_rec = dns_a()
            local aaaa_rec = dns_aaaa()
            local cname_rec = dns_cname()

            local answers = {}
            if a_rec then
                for rec in a_rec.value do table.insert(answers, rec) end
            end
            if aaaa_rec then
                for rec in aaaa_rec.value do table.insert(answers, rec) end
            end
            if cname_rec then
                for rec in cname_rec.value do table.insert(answers, rec) end
            end

            local rcode_str = ""
            if rcode then rcode_str = rcode_names[rcode.value] or tostring(rcode.value) end

            local type_str = ""
            if rtype then type_str = record_type_names[rtype.value] or tostring(rtype.value) end

            print(string.format("%s\t%s\t%s\t%s\t%s\t%s\t%s",
                t, src, dst, id,
                name and tostring(name.value) or "",
                type_str,
                table.concat(answers, ",") .. (rcode_str ~= "" and " (" .. rcode_str .. ")" or "")))
        else
            local qname = qry_name()
            local qtype = qry_type()
            local type_str = ""
            if qtype then type_str = record_type_names[qtype.value] or tostring(qtype.value) end

            print(string.format("%s\t%s\t%s\t%s\t%s\t%s\t",
                t, src, dst, id,
                qname and tostring(qname.value) or "",
                type_str))
        end
    end

    function tap.reset() end
    function tap.draw() end

end

