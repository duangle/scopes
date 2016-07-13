
-- example startup script required to load bangra support

package.path = "/path/to/bangra/extras/scite/?.lua;" .. package.path
require("script_bangra")

print("Startup script loaded.")
