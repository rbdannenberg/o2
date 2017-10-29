package ui2;

import javax.swing.*;
import javax.swing.border.EmptyBorder;
import javax.swing.event.ListSelectionEvent;
import javax.swing.table.AbstractTableModel;
import java.awt.*;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.Set;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.swing.event.ListSelectionListener;
import javax.swing.table.TableColumn;
import sun.security.util.Password;

@SuppressWarnings("serial")
public class O2_GUI extends JPanel implements ActionListener{

final JTable table = new JTable(new MyTableModel());

JTextArea output;
JPasswordField password;
private Set<String> contents = new HashSet();
public String[] myMachineDetails;
public O2_GUI() {
    initializePanel();
}

private void initializePanel() {
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(600, 600));
    setName("Configure Machines");
    JLabel passwordlabel = new JLabel("Enter the password for the local machine: ");
    password = new JPasswordField(20);
    table.setFillsViewportHeight(true);
    JScrollPane pane = new JScrollPane(table);
    JButton add = new JButton("Configure");
    add.addActionListener(this);
   // JButton add1 = new JButton("Logs");
    table.setRowSelectionAllowed(true);
    table.setColumnSelectionAllowed(false);
    table.getSelectionModel().addListSelectionListener(new SharedListSelectionHandler());
    TableColumn tc = table.getColumnModel().getColumn(3);
    tc.setCellEditor(table.getDefaultEditor(Boolean.class));
    tc.setCellRenderer(table.getDefaultRenderer(Boolean.class));
    ((JComponent) table.getDefaultRenderer(Boolean.class)).setOpaque(true);
   // tc.getCellEditor().addCellEditorListener(new CellEditorListenerImpl());

 
    JPanel command = new JPanel(new FlowLayout());
    output = new JTextArea(1, 10);
    output.setVisible(false);
    command.add(add);
   // command.add(add1);
    command.add(output);
    JPanel first = new JPanel(new FlowLayout());
    first.add(passwordlabel);
    first.add(password);
    //add(passwordlabel, BorderLayout.PAGE_START); 
   // add(password, BorderLayout.LINE_END);
    add(pane, BorderLayout.CENTER);
    add(command, BorderLayout.SOUTH);
    add(first, BorderLayout.PAGE_START);
    
    myMachineDetails = new String[4];
    String interfaceName;
    String osname = System.getProperty("os.name").toLowerCase();
    if(osname.indexOf("win") > 0) 
    {
        interfaceName = "eth0";
        osname = "windows";
    }
    else if(osname.indexOf("x") > 0) 
    {
        interfaceName = "en0";
        osname = "mac";
    }
    else
    {
        interfaceName = "en0";
        osname = "ubuntu";
    }
   
    String ip = null;
    try
    {
         NetworkInterface networkInterface = NetworkInterface.getByName(interfaceName);
         Enumeration<InetAddress> inetAddress = networkInterface.getInetAddresses();
         InetAddress currentAddress;
         currentAddress = inetAddress.nextElement();
         while(inetAddress.hasMoreElements())
         {
             currentAddress = inetAddress.nextElement();
             if(currentAddress instanceof Inet4Address && !currentAddress.isLoopbackAddress())
             {
                  ip = currentAddress.toString();
                 //System.out.println(ip.substring(1));
                 break;
             }
         } 
    }
    catch(Exception e)
    {
        e.getMessage();
    } 

    String username = System.getProperty("user.name");
    myMachineDetails[0] = ip.substring(1);
    myMachineDetails[1] = osname;
    myMachineDetails[2] = username;
    myMachineDetails[3] = new String(password.getPassword());
   // System.out.println();
 // add a nice border
    setBorder(new EmptyBorder(5, 5, 5, 5));
    CSVFile Rd = new CSVFile();
    MyTableModel NewModel = new MyTableModel();
    this.table.setModel(NewModel);
    File DataFile = new File("/Users/aparrnaa/NetBeansProjects/UI2/sample.csv");
    ArrayList<String[]> Rs2 = new ArrayList < String[]>();
   
    Rs2 = Rd.ReadCSVfile(DataFile);
    Rs2.add(myMachineDetails);
    NewModel.AddCSVData(Rs2);
    System.out.println("Rows: " + NewModel.getRowCount());
    System.out.println("Cols: " + NewModel.getColumnCount());
}

    @Override
    public void actionPerformed(ActionEvent e) {
    //try {
        String[] machineList = output.getText().split("\n");
        //ArrayList <String> machines = new ArrayList(Arrays.asList(machineList));
        this.setEnabled(false);
        O2_GUI_2_Testcases GU = new O2_GUI_2_Testcases();
        
        ArrayList <String> machines = new ArrayList(Arrays.asList(machineList));
    try {
       myMachineDetails[3]= new String(password.getPassword());
        GU.O2_GUI_2_Testcases(machines, myMachineDetails);
    } catch (IOException ex) {
        Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
    }
   
        GU.setVisible(true);
       
    } 


//reading csv file 
 class CSVFile {
    private final ArrayList<String[]> Rs = new ArrayList<String[]>();
    private String[] OneRow;

    public ArrayList<String[]> ReadCSVfile(File DataFile) {
        try {
            BufferedReader brd = new BufferedReader(new FileReader(DataFile));
            while (brd.ready()) {
                String st = brd.readLine();
                OneRow = st.split(",|\\s|;");
                Rs.add(OneRow);
                //System.out.println(Arrays.toString(OneRow));
            } // end of while
        } // end of try
        catch (Exception e) {
            String errmsg = e.getMessage();
            System.out.println("File not found:" + errmsg);
        } // end of Catch
        return Rs;
        
    }// end of ReadFile method
}// end of CSVFile class


public static void showFrame() {
    JPanel panel = new O2_GUI();
    panel.setOpaque(true);

    JFrame frame = new JFrame("Machine Configuration");
    frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
    frame.setContentPane(panel);
    frame.pack();
    frame.setVisible(true);
}

public static void main(String[] args) {
    SwingUtilities.invokeLater(new Runnable() {

        public void run() {
        	O2_GUI.showFrame();
        }
    });
}



public class MyTableModel extends AbstractTableModel {

    private String[] columns = {"MachineIP", "Type", "Username", "Password"};
  
    private ArrayList<String[]> Data = new ArrayList<String[]>();

    public void AddCSVData(ArrayList<String[]> DataIn) {
        this.Data = DataIn;
        this.fireTableDataChanged();
    }

    public int getColumnCount() {
        return columns.length;
    }
    
    public int getRowCount() {
        return Data.size();
    }

    @Override
    public String getColumnName(int col) {
        return columns[col];
    }

    @Override
    public Object getValueAt(int row, int col) {
        return Data.get(row)[col];
    }
}

class SharedListSelectionHandler implements ListSelectionListener {
        public void valueChanged(ListSelectionEvent e) { 
            ListSelectionModel lsm = (ListSelectionModel)e.getSource();
            String contents2 = new String();
            int firstIndex = e.getFirstIndex();
            int lastIndex = e.getLastIndex();
            boolean isAdjusting = e.getValueIsAdjusting(); 
            //output.append("Event for indexes "
                       //   + firstIndex + " - " + lastIndex
                         // + "; isAdjusting is " + isAdjusting
                         // + "; selected indexes:");
 
            if (lsm.isSelectionEmpty()) {
                output.append(" <none>");
            } else {
                // Find out which indexes are selected.
                int minIndex = lsm.getMinSelectionIndex();
                int maxIndex = lsm.getMaxSelectionIndex();
                for (int i = minIndex; i <= maxIndex; i++) {
                    if (lsm.isSelectedIndex(i)) {
                        for(int j = 0; j < table.getColumnCount(); j++) {
                         
                        contents2 += table.getValueAt(i, j)+" ";
                    }
                        if(isAdjusting)
                        output.append(contents2+" ");
                    }
                }
            }
            output.append("\n");
           // output.setCaretPosition(output.getDocument().getLength());
        }
    }
}