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

/**
 *
 * @author Aishu
 */
public class GetLocalMachineIP {
    public static void main(String[] args){
            String ip = null;
            try {
                String interfaceName = "eth0"; // what is the interface name?
                NetworkInterface networkInterface = NetworkInterface.getByName(interfaceName);
                Enumeration<InetAddress> inetAddress = networkInterface.getInetAddresses();
                InetAddress currentAddress;
                currentAddress = inetAddress.nextElement();
                while(inetAddress.hasMoreElements()){
                    currentAddress = inetAddress.nextElement();
                    if(currentAddress instanceof Inet4Address && !currentAddress.isLoopbackAddress()){
                        ip = currentAddress.toString();
                        System.out.println(ip.substring(1));
                        break;
                    }
                }
                ip = ip.substring(1);
            }
            catch(Exception e){
                e.getMessage();
            }
    }
}