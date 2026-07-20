#include "platform/windows/firewall_windows.h"

#include <cstdlib>

int main() {
  const TunFirewallScope invalid{
      .localAddress = "not-an-address",
      .subnetMask = "255.255.255.255",
  };
  if (ensureTunFirewallRule("ConnectTool CI invalid firewall scope", invalid)) {
    return EXIT_FAILURE;
  }

  const TunFirewallScope loopback{
      .localAddress = "127.0.0.1",
      .subnetMask = "255.255.255.255",
  };
  return ensureTunFirewallRule("ConnectTool CI native firewall smoke", loopback) ? EXIT_SUCCESS
                                                                                 : EXIT_FAILURE;
}
