-- How to run the script with tshark:
--   $ tshark -X lua_script:dns.lua

-- latest development release of Wireshark supports plugin version information
if set_plugin_info then
    local my_info = {
        version   = "1.0",
        author    = "Mohammad Bahoosh",
        email     = "moisrex at gmail",
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
    -- Print a list of tap listeners to stdout.
    -- for _,tap_name in pairs(Listener.list()) do
    --     print(tap_name)
    -- end
    
    local qry_name = Field.new("dns.qry.name")
    local resp_name = Field.new("dns.resp.name")
    local dns_a = Field.new("dns.a")
    
    local dns_packets_count = 0

    -- local tap = Listener.new("(udp or tcp) and (dst port 53 or src port 53)")
    local tap = Listener.new("dns")

    function tap.packet(pinfo, tvb, tapdata)
        dns_packets_count = dns_packets_count + 1
        query_name = qry_name()
        response = resp_name()
        a_rec = dns_a()
        if query_name then
            print("Query: " .. query_name.value)
        end
        if response then
            print("Response returned: " .. response.value)
        end
        if a_rec then
            for rec in a_rec.value do
                print("A record: " .. rec)
            end
        end
    end

    -- This function will be called at the end of the capture run
    function tap.reset()

    end

    -- this will be called at the end of the capture to print the summary
    function tap.draw()
        print()
        print("DNS Packets captured: " .. dns_packets_count)
        print("DNS lua script written by Mohammad Bahoosh")
    end

end

