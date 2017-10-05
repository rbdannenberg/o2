package events;
//lavy

import javax.swing.*;
import javax.swing.border.EmptyBorder;
import javax.swing.table.AbstractTableModel;
import java.awt.*;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import javax.swing.table.TableColumn;

@SuppressWarnings("serial")

public class O2_GUI extends JPanel {
	String MachineIPs= "";
	
	//public JTextArea output;
	final JTable table = new JTable(new MyTableModel());

public O2_GUI() {
    initializePanel();
}

public class MyTableModel extends AbstractTableModel {
    private String[] columns = {"MachineIP", "Type", "Username", "Password"};
    private ArrayList<String[]> Data = new ArrayList<String[]>();
    
    public void AddCSVData(ArrayList<String[]> DataIn) {
        this.Data = DataIn;
        this.fireTableDataChanged();
    }

    public int getColumnCount() {
        return columns.length;}
    
    public int getRowCount() {
        return Data.size();}
    
    public String getColumnName(int col) {
        return columns[col];}

    public Object getValueAt(int row, int col) {
        return Data.get(row)[col];}
}

void initializePanel() {
	ArrayList<String> MIPs =new ArrayList<String>();
	MyTableModel NewModel = new MyTableModel();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(700, 490));
    table.setFillsViewportHeight(true);
    JScrollPane pane = new JScrollPane(table);
    JButton add = new JButton("Configure");
    //JButton add1 = new JButton("Logs");
    
    add.addActionListener(new ActionListener() {
        public void actionPerformed(java.awt.event.ActionEvent evt) {
            try {
//    			jButton_add_rowsActionPerformed(evt);
    			int[] rows =table.getSelectedRows();
    			for (int i = 0; i < rows.length; i++) {
    				MachineIPs += table.getModel().getValueAt(rows[i], 0).toString()+ " ";
    				MIPs.add(table.getModel().getValueAt(rows[i], 0).toString());
				}
//    			System.out.print("1st screen:"+MachineIPs);
    			O2_GUI_2_Testcases GU = new O2_GUI_2_Testcases();
            	GU.O2_GUI_2_Testcases(MIPs);
            	GU.setVisible(true);
          } catch (IOException e) {
    			// TODO Auto-generated catch block
    			e.printStackTrace();
    		}
        }    });
    table.setRowSelectionAllowed(true);
    table.setColumnSelectionAllowed(false);
    TableColumn tc = table.getColumnModel().getColumn(3);
    JPanel command = new JPanel(new FlowLayout());
//    output = new JTextArea(1,20);
    command.add(add);
//    command.add(add1);
//    command.add(output);
    add(pane, BorderLayout.CENTER);
    add(command, BorderLayout.SOUTH);
    setBorder(new EmptyBorder(5, 5, 5, 5));
    CSVFile Rd = new CSVFile();
    this.table.setModel(NewModel);
    File DataFile = new File("C:/Users/Lavu/Desktop/CMU/CMU/O2_Project/O2_GUI/res.csv");
    ArrayList<String[]> Rs2 = Rd.ReadCSVfile(DataFile);
    NewModel.AddCSVData(Rs2);
}

//reading csv file 
public class CSVFile {
    private final ArrayList<String[]> Rs = new ArrayList<String[]>();
    private String[] OneRow;

    public ArrayList<String[]> ReadCSVfile(File DataFile) {
        try {
            BufferedReader brd = new BufferedReader(new FileReader(DataFile));
            while (brd.ready()) {
                String st = brd.readLine();
                OneRow = st.split(",|\\s|;");
                Rs.add(OneRow);
//                System.out.println(Arrays.toString(OneRow));
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
    JFrame frame = new JFrame("JTable Row Selection");
    frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
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

}}