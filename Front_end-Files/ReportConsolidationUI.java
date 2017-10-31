/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package ui2;

/**
 *
 * @author Teju
 */
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Desktop;
import java.awt.Dimension;
import java.awt.GridLayout;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Scanner;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.swing.DefaultCellEditor;
import javax.swing.JButton;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.DefaultTableModel;
import javax.swing.table.TableCellRenderer;

public class ReportConsolidationUI extends JFrame {

    private static JTable table;
	private DefaultTableModel tableModel; 

    public ReportConsolidationUI() {
        createGUI();
    }

    public void createGUI() {
        setLayout(new BorderLayout());
        JScrollPane pane = new JScrollPane();
        table = new JTable();
        pane.setViewportView(table);
       pane.setBounds(1000, 1000, 700, 700);
        JButton btnBack = new JButton("Back");
        btnBack.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {

            }
        });
        
        JPanel northPanel = new JPanel();
        JPanel southPanel = new JPanel();
        JLabel lblField1 = new JLabel("TEST RUN LOG REPORT");
       
        northPanel.add(lblField1);
        add(northPanel, BorderLayout.NORTH);
        add(southPanel, BorderLayout.SOUTH);
        add(pane, BorderLayout.CENTER);
        // tableModel = new DefaultTableModel(new Object[]{"MACHINE", "TEST CASE", "TEST RESULT", "EXECUTION LOGS", "TERMINATION TYPE"}, 0);
        tableModel = new DefaultTableModel(new Object[]{"MACHINE", "TIMESTAMP", "TEST CASE", "TEST RESULT", "EXECUTION LOGS"}, 0);
        table.setModel(tableModel);
        //String currentUsersHomeDir = System.getProperty("user.home");
        //String str= currentUsersHomeDir+"//Outputs";
        Desktop desktop = Desktop.getDesktop();
        String str = "C:/Users/Lavu/workspace/newEclipse/O2_TestEnv/outputs/Outputs/Outputs";
        File directory = new File(str);
        File[] fList = directory.listFiles();
        for (File file : fList) {
           // System.out.println("First Directory..." + file.getName());
            String machinenames = file.getName();
            File machineDirectory = new File(str + "/" + machinenames);
            File[] timeList = machineDirectory.listFiles();
            if (timeList != null) {
                for (File timefile : timeList) {
                   // System.out.println("Timestamp Directory..." + timefile.getName());
                    String testfilenames = timefile.getName();
                    File testDirectory = new File(machineDirectory + "/" + testfilenames);
                    File[] testList = testDirectory.listFiles();
                    if (testList != null) {
                        for (File testfile : testList) {
                           // System.out.println("Test Directory..." + testfile.getName());

                            //status checking
                            if (testfile.getName().endsWith(".txt")) {
                                // String filename = testfile.getName();
                                JButton openLog = new JButton("Log file");
                                openLog.setPreferredSize(new Dimension(30, 30));
                                // to get the machine IP address executed in 
                                File finalFile = new File(testDirectory + "/" +testfile.getName());
                                // check the log contents to find out the test execution status (pass/fail)
                                int fails = 0;
                                String message = "";
                                //String search = "fail";
                                List<String> search = Arrays.asList("fail", "err", "failure","stack","trace",
                                		"error","failed","core","exception","dumped","segmentation");
                                
                                try {
                                    Scanner scanner = new Scanner(testfile);
                                    while (scanner.hasNextLine()) {
                                        String line = scanner.nextLine();
                                        for(int i=0;i<search.size();i++){
                                        if (line.toLowerCase().indexOf(search.get(i).toLowerCase()) != -1) {
                                            fails++;
                                        }
                                        }
                                    }
                                } catch (FileNotFoundException e) {
                                    message = "This file does not exist!";
                                }
                                
                                if (fails == 0) {
                                    message = "Test case passed";
                                      
                                } else {
                                    message = "Test case failed";
                                }
                                 JPanel btnPanel = new JPanel();
                                 btnPanel.setLayout(new GridLayout(10, 10));
                                 btnPanel.add(openLog);
                                tableModel.addRow(new Object[]{machinenames, timefile.getName(), testfile.getName(), message, finalFile});
                                table.getColumnModel().getColumn(4).setCellRenderer(new ButtonRenderer());
                                table.getColumnModel().getColumn(4).setCellEditor(new ButtonEditor(new JTextField()));
                                
                                JScrollPane jpane = new JScrollPane(table);
                                getContentPane().add(jpane);
                                setSize(450,100);
                                setDefaultCloseOperation(EXIT_ON_CLOSE);
                                
                                openLog.addActionListener(new ActionListener() {

                                    @Override
                                    public void actionPerformed(ActionEvent arg0) {
                                        try {
                                            desktop.open(finalFile);
                                        } catch (IOException e) {
                                            e.printStackTrace();
                                        }
                                    }
                                });
                              // southPanel.add(openLog);
                                
                            }
                        }
                    }
                }
            }
        }
        southPanel.add(btnBack);
        JButton btnStartNewTest = new JButton("Start new test");
        btnStartNewTest.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {

            }
        });
        
        
        btnStartNewTest.setToolTipText("Click to start a new test execution");
        btnStartNewTest.setBounds(323, 227, 101, 23);
        southPanel.add(btnStartNewTest);

        JButton btnGetConsolidatedReport = new JButton("Get report");
        btnGetConsolidatedReport.setToolTipText("Click to get a consolidated report");
        btnGetConsolidatedReport.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {

            }
        });
        btnGetConsolidatedReport.setBounds(300, 300, 150, 50);
        southPanel.add(btnGetConsolidatedReport);
        
    }
   
        class ButtonRenderer extends JButton implements TableCellRenderer{
        
        public ButtonRenderer() {
            setOpaque(true);
        }
        
        @Override
        public Component getTableCellRendererComponent(JTable table, Object value, boolean isSelected, boolean hasFocus, int row, int column) {
                 
        setText((value == null) ?  "":value.toString());
        return this;
        }
        
        
    }

    class ButtonEditor extends DefaultCellEditor{
        protected JButton btn;
        private String lbl;
        private Boolean clicked;
        public Desktop desktop; 

        public ButtonEditor(JTextField textField) {
            super(textField);
               btn = new JButton();
               btn.setOpaque(true);
               btn.addActionListener(new ActionListener() {
                                    @Override
                                    public void actionPerformed(ActionEvent e) {
                                      fireEditingStopped();
                                    }
                                });        
        }

        @Override
        public Component getTableCellEditorComponent(JTable table, Object value, boolean isSelected, int row, int column) {
            
            lbl = (value == null)? "":value.toString();
            btn.setText(lbl);
            clicked = true;
            desktop = Desktop.getDesktop();
            return btn; 
        }

        @Override
        public Object getCellEditorValue() {
            if(clicked) {
                try {
                    File str = new File(lbl);
                    desktop.open(str);
                    
                    //   JOptionPane.showMessageDialog(btn, lbl+" Clicked");
                    clicked = false;
                    return new String(lbl);
                } catch (IOException ex) {
                    Logger.getLogger(ReportConsolidationUI.class.getName()).log(Level.SEVERE, null, ex);
                }
            }
            return super.getCellEditorValue(); //To change body of generated methods, choose Tools | Templates.
        }

        @Override
        public boolean stopCellEditing() {
            clicked = false;
            return super.stopCellEditing(); //To change body of generated methods, choose Tools | Templates.
        }

        @Override
        protected void fireEditingStopped() {
            super.fireEditingStopped(); //To change body of generated methods, choose Tools | Templates.
        }
        

        
    }
   // added now- lavy
    
    private static final int STATUS_COL = 3;
    
    private static JTable getNewRenderedTable(final JTable table) {
        table.setDefaultRenderer(Object.class, new DefaultTableCellRenderer(){
            @Override
            public Component getTableCellRendererComponent(JTable table,
                    Object value, boolean isSelected, boolean hasFocus, int row, int col) {
                super.getTableCellRendererComponent(table, value, isSelected, hasFocus, row, col);
                String status = (String)table.getModel().getValueAt(row, STATUS_COL);
                String pass ="Test case passed";
                if (!pass.equals(status)) {
                    setBackground(Color.RED);
                    setForeground(Color.WHITE);
                } else {
                    setBackground(table.getBackground());
                    setForeground(table.getForeground());
                }       
                return this;
            }   
        });
        return table;
        
    }
    
   
   
    
    public static void main(String[] args) {
        SwingUtilities.invokeLater(new Runnable() {

            @Override
            public void run() {
                ReportConsolidationUI frm = new ReportConsolidationUI();
                frm.setLocationByPlatform(true);
                frm.pack();
                frm.setDefaultCloseOperation(EXIT_ON_CLOSE);
                frm.setVisible(true);
                frm.setExtendedState(JFrame.MAXIMIZED_BOTH); 
                table = getNewRenderedTable(table);

            }

        });
           }
}
