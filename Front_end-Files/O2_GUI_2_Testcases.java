package ui2;
import java.io.File;

import java.io.IOException;
import java.util.ArrayList;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JFrame;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.table.DefaultTableModel;
import javax.swing.table.TableModel;
import javax.swing.GroupLayout.Alignment;
import javax.swing.GroupLayout;
import javax.swing.LayoutStyle.ComponentPlacement;
import java.awt.event.ActionListener;
import java.awt.Dimension;
import java.awt.event.ActionEvent;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.logging.Level;
import java.util.logging.Logger;
import java.util.stream.Collectors;
import javax.swing.ComboBoxModel;
/**
 * @author Lavanya
 */
@SuppressWarnings("serial")
public class O2_GUI_2_Testcases extends JFrame {
        ArrayList <String> machineIP = new ArrayList();
        HashMap <String, ArrayList<String>> machineAndTest = new HashMap();
	String ip;
        public O2_GUI_2_Testcases() {
	}

    @SuppressWarnings("unchecked")
	public void O2_GUI_2_Testcases(ArrayList arr) throws IOException {
    	initComponents(arr);
    }

    @SuppressWarnings("unchecked") 
    void initComponents(ArrayList<String> arr) throws IOException { 
//    	O2_GUI GU = new O2_GUI();
    	
//    	System.out.println("ArrayList:"+arr);
    	String[] array = new String[arr.size()];
    	for(int i = 0; i < arr.size(); i++) {
    	    array[i] = (String) arr.get(i);
    	}
        for(String t : array)
        {
            String s[] = t.split("\\s");
            machineIP.add(s[0]);
        }
        System.out.println(machineIP.toString());
        String[] ipList = machineIP.toArray(new String[machineIP.size()]);
    	JComboBox SelectTc = new JComboBox(ipList);
    	File folder = new File("test2");
    	File[] listOfFiles = folder.listFiles();
    	ArrayList<String> list = new ArrayList<String>();
    	
    	for (File file : listOfFiles) {
    		String filename = file.getName();
    		String ext = "c";
    		String extension = filename.substring(filename.lastIndexOf(".") + 1, filename.length());
    		if (ext.equals(extension)) {
    				list.add(filename);
    	    }
    	}
   	
        jScrollPane1 = new JScrollPane();
        jTable1 = new JTable();
        jButton_add_rows = new JButton();
        jScrollPane2 = new JScrollPane();
        jTable2 = new JTable();
        
        DefaultTableModel model = new DefaultTableModel(new Object[]{"Testcases"}, 0);
        for(String item : list){
             model.addRow(new Object[]{item});
        }
    	this.jTable1.setModel(model);
        setDefaultCloseOperation(javax.swing.WindowConstants.EXIT_ON_CLOSE);
        jScrollPane1.setViewportView(jTable1);
        jButton_add_rows.setText("Remove Selected");
        jButton_add_rows.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
             
                jButton_remove_rowsActionPerformed(evt);
            }
        });
        jTable2.setModel(new javax.swing.table.DefaultTableModel(
                new Object [][] {},
                new String [] {
                    "Selected Testcases"
                }
            ));
        jScrollPane2.setViewportView(jTable2);
        
        JButton btnRemoveAll = new JButton();
        btnRemoveAll.setText("Remove All");
        btnRemoveAll.addActionListener(new ActionListener() {
        	public void actionPerformed(ActionEvent evt) {
        		jButton_removeall_rowsActionPerformed(evt);
        	}
        });
        
        JButton btnAddAllRows = new JButton();
        btnAddAllRows.setText("Add All");
        btnAddAllRows.addActionListener(new ActionListener() {
        	public void actionPerformed(ActionEvent evt) {
        		jButton_addall_rowsActionPerformed(evt);
        	}
        });
        
        JButton btnRemoveSelected = new JButton();
        btnRemoveSelected.setText("Add Selected");
        btnRemoveSelected.addActionListener(new ActionListener() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent evt) {
                ip = (String) SelectTc.getSelectedItem();
                jButton_add_rowsActionPerformed(evt);
            }
        });
        
        JButton Execute = new JButton();
        Execute.setText("Execute");
        Execute.addActionListener(new ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
                System.out.println(machineAndTest.toString());
                String[] machineDetails = new String[array.length];
                for(String t : array)
                {
                   machineDetails = t.split("\\s");
                }
                System.out.println(machineDetails);
                ArrayList <String> tests = machineAndTest.get(machineDetails[0]);
                String testcase = tests.stream().collect(Collectors.joining(","));
                //for(String testcase : tests)
               // {
                    String[] command = {"/Users/aparrnaa/Desktop/CMU/Practicum/BACKEND/configure_script.sh", machineDetails[0], machineDetails[1], machineDetails[3], machineDetails[2], testcase};
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
                    System.out.println("Output of running command is: ");
                    try {
                        while ((line = br.readLine()) != null) {
                            System.out.println(line);
                        }
                    } catch (IOException ex) {
                        Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
                    }


                   System.out.println("Error in running command is: ");
                    try {
                    BufferedReader br2 = new BufferedReader(new InputStreamReader(p2.getErrorStream()));
                    while ((line = br2.readLine()) != null) {
                    System.out.println(line);
                    }
                    } catch (IOException ex) {
                    Logger.getLogger(O2_GUI.class.getName()).log(Level.SEVERE, null, ex);
                    }
               // }
         
            }
        });
        
        
        JButton Back = new JButton();
        Back.setText("Back");
        Back.addActionListener(new ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
//            	jButton_Back_rowsActionPerformed(evt);
            	
            	O2_GUI GU1 = new O2_GUI();
            	setVisible(false);
        		GU1.frame.setVisible(true);
        		
        		toFront();
        		requestFocus();
        		repaint();
            }    });
        
//        private void jButton_Back_rowsActionPerformed(java.awt.event.ActionEvent evt) {       
//        	O2_GUI GU1 = new O2_GUI();
//    		GU1.setVisible(true);
//            }
        
        javax.swing.GroupLayout layout = new javax.swing.GroupLayout(getContentPane());
        
        layout.setHorizontalGroup(
        	layout.createParallelGroup(Alignment.LEADING)
        		.addGroup(layout.createSequentialGroup()
        			.addContainerGap()
        			.addComponent(jScrollPane1, GroupLayout.PREFERRED_SIZE, 265, GroupLayout.PREFERRED_SIZE)
        			.addGap(20)
        			.addGroup(layout.createParallelGroup(Alignment.LEADING)	
        				.addComponent(SelectTc, GroupLayout.DEFAULT_SIZE, 250, GroupLayout.PREFERRED_SIZE)
        				.addGap(20)
        				.addComponent(jButton_add_rows, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        				.addComponent(btnRemoveSelected, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        				.addComponent(btnAddAllRows, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        				.addComponent(btnRemoveAll, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
            			.addComponent(Execute, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE)
        			.addComponent(Back, GroupLayout.DEFAULT_SIZE, 125, Short.MAX_VALUE))  
        			.addPreferredGap(ComponentPlacement.RELATED)
        			.addComponent(jScrollPane2, GroupLayout.PREFERRED_SIZE, 265, GroupLayout.PREFERRED_SIZE)        			
        			.addGap(31))
        );
        layout.setVerticalGroup(
        	layout.createParallelGroup(Alignment.LEADING)
        		.addGroup(layout.createSequentialGroup()
        			.addGroup(layout.createParallelGroup(Alignment.LEADING)
        				.addGroup(layout.createSequentialGroup()
        					.addGap(95)
        					.addComponent(SelectTc)
        					.addGap(80)
        					.addComponent(btnRemoveSelected)
        					.addPreferredGap(ComponentPlacement.RELATED)
        					.addComponent(btnAddAllRows, GroupLayout.DEFAULT_SIZE, 23, Short.MAX_VALUE)
        					.addPreferredGap(ComponentPlacement.RELATED)
        					.addComponent(jButton_add_rows)
        					.addPreferredGap(ComponentPlacement.RELATED)
        					.addComponent(btnRemoveAll, GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE)
        					.addGap(20)
        					.addComponent(Execute, GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE)
        					.addGap(20)
        					.addComponent(Back, GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE)
        					.addGap(100))
        				.addGroup(layout.createSequentialGroup()
        					.addContainerGap()
        					.addGroup(layout.createParallelGroup(Alignment.BASELINE)
        						.addComponent(jScrollPane2, GroupLayout.PREFERRED_SIZE, GroupLayout.DEFAULT_SIZE, GroupLayout.PREFERRED_SIZE)
        						.addComponent(jScrollPane1, GroupLayout.PREFERRED_SIZE, GroupLayout.DEFAULT_SIZE, GroupLayout.PREFERRED_SIZE))))
        			.addGap(39))
        );
        getContentPane().setLayout(layout);

        pack();
    }// </editor-fold>                        

	private void jButton_add_rowsActionPerformed(java.awt.event.ActionEvent evt) {                                                 

        TableModel model = jTable1.getModel();
        int[] indexs = jTable1.getSelectedRows();
                
        Object[] row = new Object[1];
        DefaultTableModel model2 = (DefaultTableModel) jTable2.getModel();
        for(int i = 0; i < indexs.length; i++)
        {
            row[0] = model.getValueAt(indexs[i], 0);
            if(machineAndTest.containsKey(ip))
                {
                    ArrayList <String> testcases = machineAndTest.get(ip);
                    for(Object o : row)
                    {
                     String temp = (String) o;
                     temp = temp.substring(0, temp.length()-2).trim();
                     testcases.add(temp);
                    }
                    
                }
            else
            {
                 ArrayList <String> testcases = new ArrayList();
                 for(Object o : row)
                 {
                     String temp = (String) o;
                     temp = temp.substring(0, temp.length()-2).trim();
                     testcases.add(temp);
                 }
                    
                 machineAndTest.put(ip, testcases);
            }
            
            model2.addRow(row);
        } 
    }    
    
    private void jButton_addall_rowsActionPerformed(java.awt.event.ActionEvent evt) {                                                  
//        TableModel model = jTable1.getModel();
        int rowCount = jTable1.getRowCount();
        Object[] row = new Object[rowCount];
        System.out.println(jTable1.getValueAt(0,0));
        for(int k=0;k<rowCount;k++){
        row[k]=jTable1.getValueAt(k,0);
        }
        for(int k=0;k<rowCount;k++){
            System.out.println(row[k]);
            }
        
        DefaultTableModel model2 = (DefaultTableModel)jTable2.getModel();
        for(int i = 0; i < row.length; i++)
        {
            row[0] = row[i];
            model2.addRow(row);
        } 
    }  
    
    
    private void jButton_remove_rowsActionPerformed(java.awt.event.ActionEvent evt) {
//        TableModel model = jTable2.getModel();
        int[] indexs = jTable2.getSelectedRows();
//        Object[] row = new Object[1];
        DefaultTableModel model2 = (DefaultTableModel) jTable2.getModel();        
        for(int i = 0; i < indexs.length; i++)
        {
        	model2.removeRow(jTable2.getSelectedRow());
        }
    } 
    
   
    private void jButton_removeall_rowsActionPerformed(java.awt.event.ActionEvent evt) {                                                       
        DefaultTableModel model2 = (DefaultTableModel)jTable2.getModel();
        int rowCount = model2.getRowCount();
        for (int i = rowCount - 1; i >= 0; i--) {
        	model2.removeRow(i);
        }
    } 
    
    
   
    
    /**
     * @param args the command line arguments
     */
    public static void main(String args[]) {
        try {
            for (javax.swing.UIManager.LookAndFeelInfo info : javax.swing.UIManager.getInstalledLookAndFeels()) {
                if ("Nimbus".equals(info.getName())) {
                    javax.swing.UIManager.setLookAndFeel(info.getClassName());
                    break;
                }
            }
        } catch (ClassNotFoundException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (InstantiationException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (IllegalAccessException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        } catch (javax.swing.UnsupportedLookAndFeelException ex) {
            java.util.logging.Logger.getLogger(O2_GUI_2_Testcases.class.getName()).log(java.util.logging.Level.SEVERE, null, ex);
        }
        //</editor-fold>

        /* Create and display the form */
        java.awt.EventQueue.invokeLater(new Runnable() {
            public void run() {
                new O2_GUI_2_Testcases().setVisible(true);
            }
        });
    }

    // Variables declaration - do not modify                     
    private javax.swing.JButton jButton_add_rows;
    private javax.swing.JScrollPane jScrollPane1;
    private javax.swing.JScrollPane jScrollPane2;
    private javax.swing.JTable jTable1;
    private javax.swing.JTable jTable2;

}