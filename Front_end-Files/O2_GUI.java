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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.swing.event.ListSelectionListener;
import javax.swing.table.TableColumn;

@SuppressWarnings("serial")
public class O2_GUI extends JPanel implements ActionListener{

final JTable table = new JTable(new MyTableModel());
JTextArea output;
private Set<String> contents = new HashSet();

public O2_GUI() {
    initializePanel();
}

private void initializePanel() {
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(600, 600));
    setName("Configure Machines");


    table.setFillsViewportHeight(true);
    JScrollPane pane = new JScrollPane(table);
    JButton add = new JButton("Configure");
    add.addActionListener(this);
    JButton add1 = new JButton("Logs");

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
    command.add(add);
    command.add(add1);
    command.add(output);

    add(pane, BorderLayout.CENTER);
    add(command, BorderLayout.SOUTH);
    
 // add a nice border
    setBorder(new EmptyBorder(5, 5, 5, 5));
    CSVFile Rd = new CSVFile();
    MyTableModel NewModel = new MyTableModel();
    this.table.setModel(NewModel);
    File DataFile = new File("/Users/aparrnaa/NetBeansProjects/UI2/sample.csv");
    ArrayList<String[]> Rs2 = Rd.ReadCSVfile(DataFile);
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
        GU.O2_GUI_2_Testcases(machines);
    } catch (IOException ex) {
        Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
    }
   
        GU.setVisible(true);
       //  System.out.println("ip"+machineDetails[0]);
        //System.out.println("os:"+machineDetails[1]);
        //System.out.println("username:"+machineDetails[2]);
       // System.out.println("password:"+machineDetails[3]);
        /*try {
        String line;
        Process p = Runtime.getRuntime().exec("/Users/aparrnaa/Desktop/CMU/Practicum/o2_MAIN_COPY/BACKEND/configure_script.sh"+" "+machineDetails[0]+","+machineDetails[1]+","+machineDetails[2]+","+machineDetails[3]);
        BufferedReader in = new BufferedReader(
        new InputStreamReader(p.getInputStream()) );
        
        while ((line = in.readLine()) != null) {
        System.out.println(line);
        }
        in.close();
        
        BufferedReader err = new BufferedReader(
        new InputStreamReader(p.getErrorStream()) );
        
        while ((line = err.readLine()) != null) {
        System.out.println(line);
        }
        OutputStream outputStream = p.getOutputStream();
        PrintStream printStream = new PrintStream(outputStream);
        printStream.println();
        printStream.flush();
        printStream.close();

        }
        catch (IOException ex) {
        Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
        } */
     /*   String[] command = {"/Users/aparrnaa/Desktop/CMU/Practicum/o2_MAIN_COPY/BACKEND/configure_script.sh", machineDetails[0], machineDetails[1], machineDetails[3], machineDetails[2]};
        ProcessBuilder p = new ProcessBuilder(command);
        Process p2 = null;
        try {
            p2 = p.start();
        } catch (IOException ex) {
            System.out.println(ex.getMessage());
            Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
        }
        BufferedReader br = new BufferedReader(new InputStreamReader(p2.getInputStream()));
        String line;
        p.redirectErrorStream(true);
        System.out.println("Output of running " + command + " is: ");
        try {
            while ((line = br.readLine()) != null) {
                System.out.println(line);
            }
        } catch (IOException ex) {
            Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
        }
 
        
       System.out.println("Error in running " + command + " is: ");
        try {
        BufferedReader br2 = new BufferedReader(new InputStreamReader(p2.getErrorStream()));
        while ((line = br2.readLine()) != null) {
        System.out.println(line);
        }
        } catch (IOException ex) {
        Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
        }  */
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