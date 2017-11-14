/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package UI_Wireframes;

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
import java.util.Arrays;
import java.util.List;
import java.util.Scanner;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.swing.DefaultCellEditor;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.JTextField;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.DefaultTableModel;
import javax.swing.table.TableCellRenderer;
import java.util.ArrayList;
import javax.swing.JFrame;
import javax.swing.JOptionPane;
import javax.swing.SwingUtilities;

/**
 *
 * @author Aishu
 */
public class TestResultsScreen extends javax.swing.JFrame {

    private static JTable table;
    private DefaultTableModel tableModel; 
    private JButton btnStartNewTest; 
    
    /**
     * Creates new form ReportGen_Old
     */
    
    public TestResultsScreen() {
        //initComponents();
        createGUI();
        table = getNewRenderedTable(table);
    }

    public void createGUI() {
        setPreferredSize(new java.awt.Dimension(1040, 560));
        setMaximumSize(new Dimension(1040, 560));
        setMinimumSize(new Dimension(1040, 560));
        setLayout(new BorderLayout());
        JScrollPane pane = new JScrollPane();
        table = new JTable();
        pane.setViewportView(table);
        pane.setBounds(1000, 1000, 700, 700);
        JButton btnBack = new JButton("Back");
        btnBack.setVisible(false);
        btnBack.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {
                BackButton(e);
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
        final Desktop desktop = Desktop.getDesktop();
        String str = "Outputs";
        File directory = new File(str);
        File[] fList = directory.listFiles();
        for (File file : fList) {
           System.out.println("First Directory..." + file.getName());
            String machinenames = file.getName();
            File machineDirectory = new File(str + "/" + machinenames);
            File[] timeList = machineDirectory.listFiles();
            if (timeList != null) {
                for (File timefile : timeList) {
                   System.out.println("Timestamp Directory..." + timefile.getName());
                    String testfilenames = timefile.getName();
                    File testDirectory = new File(machineDirectory + "/" + testfilenames);
                    File[] testList = testDirectory.listFiles();
                    if (testList != null) {
                        for (File testfile : testList) {
                            System.out.println("Test Directory..." + testfile.getName());

                            //status checking
                            if (testfile.getName().endsWith(".txt")) {
                                // String filename = testfile.getName();
                                JButton openLog = new JButton("Log file");
                                openLog.setPreferredSize(new Dimension(30, 30));
                                // to get the machine IP address executed in 
                                final File finalFile = new File(testDirectory + "/" +testfile.getName());
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
        btnStartNewTest = new JButton("Start new test");
        btnStartNewTest.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {
               StartNewTestButton(e);
            }
        });
            
        btnStartNewTest.setToolTipText("Click to start a new test execution");
        btnStartNewTest.setBounds(323, 227, 101, 23);
        southPanel.add(btnStartNewTest);

        JButton btnGetConsolidatedReport = new JButton("Get report");
        btnGetConsolidatedReport.setVisible(false);
        btnGetConsolidatedReport.setToolTipText("Click to get a consolidated report");
        btnGetConsolidatedReport.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {

            }
        });
        btnGetConsolidatedReport.setBounds(300, 300, 150, 50);
        southPanel.add(btnGetConsolidatedReport);
        
    }
    
    /**
     * This method is called from within the constructor to initialize the form.
     * WARNING: Do NOT modify this code. The content of this method is always
     * regenerated by the Form Editor.
     */
    @SuppressWarnings("unchecked")
    // <editor-fold defaultstate="collapsed" desc="Generated Code">//GEN-BEGIN:initComponents
    private void initComponents() {

        setDefaultCloseOperation(javax.swing.WindowConstants.EXIT_ON_CLOSE);

        javax.swing.GroupLayout layout = new javax.swing.GroupLayout(getContentPane());
        getContentPane().setLayout(layout);
        layout.setHorizontalGroup(
            layout.createParallelGroup(javax.swing.GroupLayout.Alignment.LEADING)
            .addGap(0, 400, Short.MAX_VALUE)
        );
        layout.setVerticalGroup(
            layout.createParallelGroup(javax.swing.GroupLayout.Alignment.LEADING)
            .addGap(0, 300, Short.MAX_VALUE)
        );

        pack();
    }// </editor-fold>//GEN-END:initComponents

     private void StartNewTestButton(java.awt.event.ActionEvent evt) {
    // TODO add your handling code here:
            SelectTestSuiteScreen testexecScreen = new SelectTestSuiteScreen();
            testexecScreen.setVisible(true);
            this.setVisible(false);
    }                                        
    
    private void BackButton(java.awt.event.ActionEvent evt) {
    // TODO add your handling code here:
            CreateTestSuiteScreen testexecScreen = new CreateTestSuiteScreen();
            testexecScreen.setVisible(true);
            this.setVisible(false);
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
                    Logger.getLogger(TestResultsScreen.class.getName()).log(Level.SEVERE, null, ex);
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
    
    
    private static final int STATUS_COL = 3;
    private static JTable getNewRenderedTable(final JTable table) {
        System.out.println("The table is getting rendered .. ");
        table.setDefaultRenderer(Object.class, new DefaultTableCellRenderer(){
            @Override
            public Component getTableCellRendererComponent(JTable table,
                    Object value, boolean isSelected, boolean hasFocus, int row, int col) {
                super.getTableCellRendererComponent(table, value, isSelected, hasFocus, row, col);
                String status = (String)table.getModel().getValueAt(row, STATUS_COL);
                String pass ="Test case passed";
                if (!pass.equals(status)) {
                    System.out.println("One of my cases fail");
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
    
    /**
     * @param args the command line arguments
     */
    public static void main(String args[]) {
        /* Set the Nimbus look and feel */
        //<editor-fold defaultstate="collapsed" desc=" Look and feel setting code (optional) ">
        /* If Nimbus (introduced in Java SE 6) is not available, stay with the default look and feel.
         * For details see http://download.oracle.com/javase/tutorial/uiswing/lookandfeel/plaf.html 
         */
        try {
            for (javax.swing.UIManager.LookAndFeelInfo info : javax.swing.UIManager.getInstalledLookAndFeels()) {
                if ("Nimbus".equals(info.getName())) {
                    javax.swing.UIManager.setLookAndFeel(info.getClassName());
                    break;
                }
            }
        } catch (ClassNotFoundException ex) {
            java.util.logging.Logger.getLogger(TestResultsScreen.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (InstantiationException ex) {
            java.util.logging.Logger.getLogger(TestResultsScreen.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (IllegalAccessException ex) {
            java.util.logging.Logger.getLogger(TestResultsScreen.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (javax.swing.UnsupportedLookAndFeelException ex) {
            java.util.logging.Logger.getLogger(TestResultsScreen.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        }
        //</editor-fold>
        //</editor-fold>

        /* Create and display the form */
        java.awt.EventQueue.invokeLater(new Runnable() {
            public void run() {
                TestResultsScreen frm = new TestResultsScreen();
                frm.setDefaultCloseOperation(EXIT_ON_CLOSE);
                frm.setVisible(true);
                table = getNewRenderedTable(table);
                
            }
        });
    }

    // Variables declaration - do not modify//GEN-BEGIN:variables
    // End of variables declaration//GEN-END:variables
}
