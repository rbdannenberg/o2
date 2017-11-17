/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package POCs;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Enumeration;
import java.net.*;
import java.io.*;
import java.util.*;
import java.net.InetAddress;
 

/**
 *
 * @author Aishu
 */
public class GetLocalMachineIP {
    public static void main(String[] args) throws UnknownHostException{
            /*
            String ip = null;
            try {
                String interfaceName = "eth0"; // what is the interface name?
                NetworkInterface networkInterface = NetworkInterface.getByName(interfaceName);
                System.out.println("NetworkInterface: " + networkInterface.getName());
                Enumeration<InetAddress> inetAddress = networkInterface.getInetAddresses();
                InetAddress currentAddress;
                currentAddress = inetAddress.nextElement();
                while(inetAddress.hasMoreElements()){
                    currentAddress = inetAddress.nextElement();
                    if(currentAddress instanceof Inet4Address && !currentAddress.isLoopbackAddress()){
                        ip = currentAddress.toString();
                        System.out.println("ip substring is : " + ip.substring(1));
                        break;
                    }
                }
                ip = ip.substring(1);
                System.out.println("This substring gives me : " + ip);
            }
            catch(Exception e){
                e.getMessage();
            }*/
        
            // Returns the instance of InetAddress containing
            // local host name and address
            InetAddress localhost = InetAddress.getLocalHost();
            System.out.println("System IP Address : " +
                          (localhost.getHostAddress()).trim());

            // Find public IP address
            String systemipaddress = "";
            try
            {
                URL url_name = new URL("http://bot.whatismyipaddress.com");

                BufferedReader sc =
                new BufferedReader(new InputStreamReader(url_name.openStream()));

                // reads system IPAddress
                systemipaddress = sc.readLine().trim();
            }
            catch (Exception e)
            {
                systemipaddress = "Cannot Execute Properly";
            }
            System.out.println("Public IP Address: " + systemipaddress +"\n");
    }
}